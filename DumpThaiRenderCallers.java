import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import java.util.LinkedHashMap;
import java.util.Map;

public class DumpThaiRenderCallers extends GhidraScript {
    @Override
    public void run() throws Exception {
        Address render = toAddr(0x00434120);
        Address layout = toAddr(0x00434cd0);
        Function renderFunction = getFunctionAt(render);
        Function layoutFunction = getFunctionAt(layout);
        println("RENDER " + render + " " + (renderFunction == null ? "<none>" : renderFunction.getName()));
        println("LAYOUT " + layout + " " + (layoutFunction == null ? "<none>" : layoutFunction.getName()));

        Listing listing = currentProgram.getListing();
        for (Instruction instruction : listing.getInstructions(render, true)) {
            if (instruction.getAddress().subtract(render) > 0x80) break;
            println("INSN " + instruction.getAddress() + " " + instruction);
        }

        Map<String, Function> functions = new LinkedHashMap<>();
        ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(render);
        while (refs.hasNext()) {
            Reference ref = refs.next();
            Function caller = getFunctionContaining(ref.getFromAddress());
            println("XREF " + ref.getFromAddress() + " " + ref.getReferenceType() +
                " caller=" + (caller == null ? "<none>" : caller.getName() + " @ " + caller.getEntryPoint()));
            if (caller != null) functions.put(caller.getEntryPoint().toString(), caller);
        }
        if (layoutFunction != null) functions.put(layoutFunction.getEntryPoint().toString(), layoutFunction);
        if (renderFunction != null) functions.put(renderFunction.getEntryPoint().toString(), renderFunction);

        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        for (Function function : functions.values()) {
            monitor.checkCancelled();
            DecompileResults result = decompiler.decompileFunction(function, 45, monitor);
            println("\nDECOMPILE " + function.getName() + " @ " + function.getEntryPoint() +
                " completed=" + result.decompileCompleted());
            if (result.getErrorMessage() != null && !result.getErrorMessage().isEmpty()) {
                println("ERROR " + result.getErrorMessage());
            }
            if (result.getDecompiledFunction() != null) println(result.getDecompiledFunction().getC());
        }
        decompiler.dispose();
    }
}
