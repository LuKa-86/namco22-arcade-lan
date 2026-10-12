// SeedTables.java -- Ghidra headless script: find the code a 68020 program only reaches through its DISPATCH TABLES.
//
// These games call their mode / task / object routines as  jsr ([bd,PC,Xn.w*4])  (and jmp): a table of 32-bit routine addresses
// right in the code, indexed by a state word. Ghidra's analysis does not follow them (and decodes that addressing mode as
// post-indexed: tools/gen/lift68k.py fix_preindexed), so everything behind a table is never disassembled.
//
// Each round: every such instruction's table is read -- entries while they are even addresses inside the image that hold a
// plausible first opcode -- each entry is disassembled and made a function, then the auto-analysis runs again (new code has new
// tables). Also takes a seeds file (one hex address a line: addresses a RUN trapped on, reached through tables in RAM or
// address registers) as the script argument. Stops when a round adds nothing.
// @category Namco22
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.Memory;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;

public class SeedTables extends GhidraScript {
    private Memory mem;
    private long size;

    private boolean plausible(long t) throws Exception {
        if (t < 0x400 || t >= size || (t & 1) != 0) return false;
        int w = mem.getShort(toAddr(t)) & 0xFFFF;
        return w != 0x0000 && w != 0xFFFF;
    }

    private boolean seed(long t) throws Exception {
        Address a = toAddr(t);
        boolean added = false;
        if (currentProgram.getListing().getInstructionAt(a) == null) {
            if (currentProgram.getListing().getInstructionContaining(a) != null) return false;   // inside another instruction: not code
            new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
            added = currentProgram.getListing().getInstructionAt(a) != null;
        }
        if (currentProgram.getListing().getInstructionAt(a) != null && currentProgram.getFunctionManager().getFunctionAt(a) == null) {
            try { createFunction(a, null); added = true; } catch (Exception e) { }
        }
        return added;
    }

    // CODE CUT SHORT BY DATA. Ghidra's reference analysers mark bytes that look like addresses as data (the 00E0xxxx operand of a
    // move, say) before the code around them is known; a routine disassembled later then stops dead at that data. Every instruction
    // whose fall-through or branch target is inside the image but is no instruction: clear whatever data sits there, disassemble on.
    private int repair() throws Exception {
        int fixed = 0;
        for (int pass = 0; pass < 50; pass++) {
            java.util.ArrayList<Long> todo = new java.util.ArrayList<>();
            InstructionIterator ii = currentProgram.getListing().getInstructions(true);
            while (ii.hasNext()) {
                Instruction in = ii.next();
                Address ft = in.getFallThrough();
                if (ft != null && currentProgram.getListing().getInstructionAt(ft) == null) todo.add(ft.getOffset());
                for (Address f : in.getFlows())
                    if (f.getOffset() < size && currentProgram.getListing().getInstructionAt(f) == null) todo.add(f.getOffset());
            }
            int n = 0;
            for (long t : todo) {
                if (t < 0x400 || t >= size || (t & 1) != 0) continue;
                Address a = toAddr(t);
                if (currentProgram.getListing().getInstructionAt(a) != null) continue;
                if (currentProgram.getListing().getInstructionContaining(a) != null) continue;      // inside another instruction: leave it
                long end = Math.min(size - 1, t + 15);
                ghidra.program.model.listing.Data d = currentProgram.getListing().getDefinedDataContaining(a);
                if (d != null) end = Math.max(end, d.getMaxAddress().getOffset());
                // clear only DATA in the way, never instructions
                for (long x = t; x <= end; ) {
                    ghidra.program.model.listing.Data dd = currentProgram.getListing().getDefinedDataContaining(toAddr(x));
                    if (dd != null) { currentProgram.getListing().clearCodeUnits(dd.getMinAddress(), dd.getMaxAddress(), false); x = dd.getMaxAddress().getOffset() + 1; }
                    else x++;
                }
                new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
                if (currentProgram.getListing().getInstructionAt(a) != null) n++;
            }
            fixed += n;
            if (n == 0) break;
        }
        if (fixed > 0) println("SeedTables: repaired " + fixed + " places where code ran into data");
        return fixed;
    }

    @Override
    public void run() throws Exception {
        mem = currentProgram.getMemory();
        size = mem.getSize();
        int total = 0;
        String[] args = getScriptArgs();
        if (args.length > 0 && new File(args[0]).exists()) {
            BufferedReader r = new BufferedReader(new FileReader(args[0]));
            String line; int n = 0;
            while ((line = r.readLine()) != null) {
                line = line.trim();
                if (line.isEmpty() || line.startsWith("#")) continue;
                long t = Long.parseLong(line.split("\\s+")[0], 16);
                if (plausible(t) && seed(t)) n++;
            }
            r.close();
            println("SeedTables: " + n + " seeds from " + args[0]);
            total += n;
        }
        for (int round = 1; round <= 20; round++) {
            int added = 0, tables = 0;
            InstructionIterator ii = currentProgram.getListing().getInstructions(true);
            java.util.ArrayList<Long> targets = new java.util.ArrayList<>();
            while (ii.hasNext()) {
                Instruction in = ii.next();
                if (in.getLength() < 6) continue;
                long a = in.getAddress().getOffset();
                int op = mem.getShort(toAddr(a)) & 0xFFFF;
                if (op != 0x4EBB && op != 0x4EFB) continue;                  // jsr / jmp (d8,PC,Xn) and its full-format forms
                int ext = mem.getShort(toAddr(a + 2)) & 0xFFFF;
                if ((ext & 0x100) == 0 || (ext & 0x40) != 0) continue;       // brief format, or no index
                int iis = ext & 7;
                if (iis < 1 || iis > 3) continue;                            // not memory indirect pre-indexed
                int bdsize = (ext >> 4) & 3;
                long bd = bdsize == 2 ? (short) mem.getShort(toAddr(a + 4)) : bdsize == 3 ? mem.getInt(toAddr(a + 4)) : 0;
                if ((ext & 0x80) != 0) continue;                             // base suppressed: not PC-relative
                long table = a + 2 + bd;
                if (table < 0 || table + 4 > size) continue;
                tables++;
                for (int k = 0; k < 512; k++) {
                    long ea = table + 4L * k;
                    if (ea + 4 > size) break;
                    if (k > 0 && currentProgram.getListing().getInstructionAt(toAddr(ea)) != null) break;   // the table ran into code
                    long t = mem.getInt(toAddr(ea)) & 0xFFFFFFFFL;
                    if (!plausible(t)) break;
                    targets.add(t);
                }
            }
            for (long t : targets) if (seed(t)) added++;
            added += repair();
            println("SeedTables: round " + round + ": " + tables + " tables, " + added + " new entry points");
            total += added;
            if (added == 0) break;
            analyzeAll(currentProgram);
        }
        println("SeedTables: " + total + " entry points added in all");
    }
}
