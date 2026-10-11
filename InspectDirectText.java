import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class InspectDirectText extends GhidraScript {
    @Override
    public void run() throws Exception {
        Address entry = toAddr(0x00434500);
        Function function = getFunctionAt(entry);
        println("TEXT " + entry + " " + (function == null ? "<none>" : function.getSignature()));
        for (Instruction instruction : currentProgram.getListing().getInstructions(entry, true)) {
            if (instruction.getAddress().subtract(entry) > 0x90) break;
            println("INSN " + instruction.getAddress() + " " + instruction);
        }
        ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(entry);
        while (refs.hasNext()) {
            Reference ref = refs.next();
            Function caller = getFunctionContaining(ref.getFromAddress());
            println("CALLER " + ref.getFromAddress() + " caller=" +
                (caller == null ? "<none>" : caller.getName() + " @ " + caller.getEntryPoint()));
        }
        if (function != null) {
            DecompInterface decompiler = new DecompInterface();
            decompiler.openProgram(currentProgram);
            DecompileResults result = decompiler.decompileFunction(function, 45, monitor);
            println("DECOMPILED=" + result.decompileCompleted());
            if (result.getDecompiledFunction() != null) println(result.getDecompiledFunction().getC());
            if (result.getErrorMessage() != null) println("ERROR=" + result.getErrorMessage());
            decompiler.dispose();
        }
    }
}
