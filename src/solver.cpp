
#include "solver.hpp"
#include "globals.hpp"
#include "context.hpp"

#include <dbg.hpp>


/* This function return a vector of Inputs. A vector is necesary since switch conditions may have multiple branch constraints*/
std::vector<Input> solve_formula(ea_t pc, size_t path_constraint_index)
{
    auto pathConstrains = tritonCtx.getPathConstraints();
    std::vector<Input> solutions;
    
    if (pathConstrains.empty() || path_constraint_index >= pathConstrains.size()) {
        msg("Error. Requested path constraint index %u is larger than PathConstraints vector size (%lu)\n", path_constraint_index, pathConstrains.size());
        return solutions;
    }

    // Double check that the condition at the path constraint index is at the address the user selected
    if (pathConstrains[path_constraint_index].getBranchConstraints().empty()
        || std::get<1>(pathConstrains[path_constraint_index].getBranchConstraints()[0]) != pc) {
        msg("[!] No branch constraint at selected address " MEM_FORMAT "\n", pc);
        return solutions;
    }

    auto ast = tritonCtx.getAstContext();
    // We are going to store here the constraints for the previous conditions
    // We can not initializate this to null, so we do it to a true condition (based on code_coverage_crackme_xor.py from the triton project)
    auto previousConstraints = ast->equal(ast->bvtrue(), ast->bvtrue());

    // Add user define constraints (borrar en reejecuccion, poner mensaje if not sat, 
    if (ponce_table_chooser){
        for (const auto& [id, constrain] : ponce_table_chooser->constrains) {
            for (const auto& [abstract_node_constrain, str_constrain] : constrain) {
                previousConstraints = ast->land(previousConstraints, abstract_node_constrain);
            }
        }
    }  

    // First we iterate through the previous path constrains to add the predicates of the taken path
    unsigned int j;
    for (j = 0; j < path_constraint_index; j++)
    {
        if (cmdOptions.showExtraDebugInfo)
            msg("[+] Keeping condition %d\n", j);

        // We add to the previous constraints the predicate for the taken branch 
        auto predicate = pathConstrains[j].getTakenPredicate();
        previousConstraints = ast->land(previousConstraints, predicate);
    }

    // Then we use the predicate for the non taken path so we "solve" that condition.
    // We try to solve every non taken branch (more than one is possible under certain situations
    for (auto const& [taken, srcAddr, dstAddr, constraint] : pathConstrains[path_constraint_index].getBranchConstraints()) {
        if (!taken) {
            // We concatenate the previous constraints for the taken path plus the non taken constrain of the user selected condition
            triton::ast::SharedAbstractNode final_expr = ast->land(previousConstraints, constraint);
            if (cmdOptions.showExtraDebugInfo) {  
                std::stringstream ss;
                ss << "(set-logic QF_AUFBV)" << std::endl;
                tritonCtx.liftToSMT(ss, tritonCtx.newSymbolicExpression(final_expr), true);
                msg("[+] Formula:\n%s\n\n", ss.str().c_str());
            }

            //Time to solve
            tritonCtx.setSolverTimeout(cmdOptions.solver_timeout * 1000);
            triton::engines::solver::status_e solver_status;
            auto model = tritonCtx.getModel(final_expr, &solver_status);
            
            if (solver_status == triton::engines::solver::status_e::TIMEOUT) {
                msg("[!] Solver timed out after %d seconds\n", cmdOptions.solver_timeout);
            }
            else if (solver_status == triton::engines::solver::status_e::UNSAT) {
                msg("[!] That formula cannnot be solved (UNSAT)\n");
            }

            else if (solver_status == triton::engines::solver::status_e::SAT) {
                Input newinput;
                //Clone object 
                newinput.path_constraint_index = path_constraint_index;
                newinput.dstAddr = dstAddr;
                newinput.srcAddr = srcAddr;

                // model is an std::unordered_map. Lets sort it out so results make more sense when printed
                std::map<triton::usize, triton::engines::solver::SolverModel> ordered_model(model.begin(), model.end());

                msg("[+] Solution found! Values:\n");
                for (const auto& [symId, model] : ordered_model) {
                    triton::engines::symbolic::SharedSymbolicVariable  symbVar = tritonCtx.getSymbolicVariable(symId);
                    std::string  symbVarComment = symbVar->getComment();
                    triton::uint512 model_value = model.getValue();
                    if (symbVar->getType() == triton::engines::symbolic::variable_e::MEMORY_VARIABLE) {
                        auto mem = triton::arch::MemoryAccess(symbVar->getOrigin(), symbVar->getSize() / 8);
                        newinput.memOperand.emplace_back(mem, model_value);
                    }
                    else if (symbVar->getType() == triton::engines::symbolic::variable_e::REGISTER_VARIABLE) {
                        auto reg = triton::arch::Register(*tritonCtx.getCpuInstance(), (triton::arch::register_e)symbVar->getOrigin());
                        newinput.regOperand.emplace_back(reg, model_value);
                    }
                    switch (symbVar->getSize())
                    {
                    case 8:
                        msg(" - %s%s: %#02x %s\n", 
                            model.getVariable()->getName().c_str(), 
                            !symbVarComment.empty()? (" ("+symbVarComment+")").c_str():"",
                            static_cast<uchar>(model_value),
                            isprint(static_cast<uchar>(model_value)) ? ("(" + std::string(1, static_cast<uchar>(model_value)) + ")").c_str()  : "");
                        break;
                    case 16:
                        msg(" - %s%s: %#04x (%c%c)\n", 
                            !symbVarComment.empty() ? (" (" + symbVarComment + ")").c_str() : "",
                            symbVarComment.c_str(), 
                            static_cast<ushort>(model_value),
                            static_cast<uchar>(model_value) == 0 ? ' ' : static_cast<uchar>(model_value),
                            (unsigned char)(static_cast<ushort>(model_value) >> 8) == 0 ? ' ' : (unsigned char)(static_cast<ushort>(model_value) >> 8));
                        break;
                    case 32:
                        msg(" - %s%s: %#08x\n", 
                            !symbVarComment.empty() ? (" (" + symbVarComment + ")").c_str() : "",
                            symbVarComment.c_str(), 
                            static_cast<uint32>(model_value));
                        break;
                    case 64:
                        msg(" - %s%s: %#16llx\n", 
                            model.getVariable()->getName().c_str(), 
                            !symbVarComment.empty() ? (" (" + symbVarComment + ")").c_str() : "",
                            static_cast<uint64>(model_value));
                        break;
                    default:
                        msg("[!] Unsupported size for the symbolic variable: %s (%s)\n", model.getVariable()->getName().c_str(), symbVarComment.c_str()); // what about 128 - 512 registers? 
                    }
                }
                solutions.push_back(newinput);
            }
            else {
                msg("[!] You should not see this. If so report a bug :(\n");
            }
        }
    }
    return solutions;
}



/*This function identify the type of condition jmp and negate the flags to negate the jmp.
Probably it is possible to do this with the solver, adding more variable to the formula to
identify the flag of the conditions and get the values. But for now we are doing it in this way.*/
void negate_flag_condition(triton::arch::Instruction* triton_instruction)
{
    switch (triton_instruction->getType())
    {
    case triton::arch::x86::ID_INS_JA:
    {
        uint64 cf;
        get_reg_val("CF", &cf);
        uint64 zf;
        get_reg_val("ZF", &zf);
        if (cf == 0 && zf == 0)
        {
            cf = 1;
            zf = 1;
        }
        else
        {
            cf = 0;
            zf = 0;
        }
        set_reg_val("ZF", zf);
        set_reg_val("CF", cf);
        break;
    }
    case triton::arch::x86::ID_INS_JAE:
    {
        uint64 cf;
        get_reg_val("CF", &cf);
        uint64 zf;
        get_reg_val("ZF", &zf);
        if (cf == 0 || zf == 0)
        {
            cf = 1;
            zf = 1;
        }
        else
        {
            cf = 0;
            zf = 0;
        }
        set_reg_val("ZF", zf);
        set_reg_val("CF", cf);
        break;
    }
    case triton::arch::x86::ID_INS_JB:
    {
        uint64 cf;
        get_reg_val("CF", &cf);
        cf = !cf;
        set_reg_val("CF", cf);
        break;
    }
    case triton::arch::x86::ID_INS_JBE:
    {
        uint64 cf;
        get_reg_val("CF", &cf);
        uint64 zf;
        get_reg_val("ZF", &zf);
        if (cf == 1 || zf == 1)
        {
            cf = 0;
            zf = 0;
        }
        else
        {
            cf = 1;
            zf = 1;
        }
        set_reg_val("ZF", zf);
        set_reg_val("CF", cf);
        break;
    }
    /*	ToDo: Check this one
        case triton::arch::x86::ID_INS_JCXZ:
        {
        break;
        }*/
    case triton::arch::x86::ID_INS_JE:
    case triton::arch::x86::ID_INS_JNE:
    {
        uint64 zf;
        auto old_value = get_reg_val("ZF", &zf);
        zf = !zf;
        set_reg_val("ZF", zf);
        break;
    }
    //case triton::arch::x86::ID_INS_JRCXZ:
    //case triton::arch::x86::ID_INS_JECXZ:
    case triton::arch::x86::ID_INS_JG:
    {
        uint64 sf;
        get_reg_val("SF", &sf);
        uint64 of;
        get_reg_val("OF", &of);
        uint64 zf;
        get_reg_val("ZF", &zf);
        if (sf == of && zf == 0)
        {
            sf = !of;
            zf = 1;
        }
        else
        {
            sf = of;
            zf = 0;
        }
        set_reg_val("SF", sf);
        set_reg_val("OF", of);
        set_reg_val("ZF", zf);
        break;
    }
    case triton::arch::x86::ID_INS_JGE:
    {
        uint64 sf;
        get_reg_val("SF", &sf);
        uint64 of;
        get_reg_val("OF", &of);
        uint64 zf;
        get_reg_val("ZF", &zf);
        if (sf == of || zf == 1)
        {
            sf = !of;
            zf = 0;
        }
        else
        {
            sf = of;
            zf = 1;
        }
        set_reg_val("SF", sf);
        set_reg_val("OF", of);
        set_reg_val("ZF", zf);
        break;
    }
    case triton::arch::x86::ID_INS_JL:
    {
        uint64 sf;
        get_reg_val("SF", &sf);
        uint64 of;
        get_reg_val("OF", &of);
        if (sf == of)
        {
            sf = !of;
        }
        else
        {
            sf = of;
        }
        set_reg_val("SF", sf);
        set_reg_val("OF", of);
        break;
    }
    case triton::arch::x86::ID_INS_JLE:
    {
        uint64 sf;
        get_reg_val("SF", &sf);
        uint64 of;
        get_reg_val("OF", &of);
        uint64 zf;
        get_reg_val("ZF", &zf);
        if (sf != of || zf == 1)
        {
            sf = of;
            zf = 0;
        }
        else
        {
            sf = !of;
            zf = 1;
        }
        set_reg_val("SF", sf);
        set_reg_val("OF", of);
        set_reg_val("ZF", zf);
        break;
    }
    case triton::arch::x86::ID_INS_JNO:
    case triton::arch::x86::ID_INS_JO:
    {
        uint64 of;
        get_reg_val("OF", &of);
        of = !of;
        set_reg_val("OF", of);
        break;
    }
    case triton::arch::x86::ID_INS_JNP:
    case triton::arch::x86::ID_INS_JP:
    {
        uint64 pf;
        get_reg_val("PF", &pf);
        pf = !pf;
        set_reg_val("PF", pf);
        break;
    }
    case triton::arch::x86::ID_INS_JNS:
    case triton::arch::x86::ID_INS_JS:
    {
        uint64 sf;
        get_reg_val("SF", &sf);
        sf = !sf;
        set_reg_val("SF", sf);
        break;
    }
    default:
        msg("[!] We cannot negate %s instruction\n", triton_instruction->getDisassembly().c_str());
    }
}


/*We set the memory to the results we got and do the analysis from there*/
void set_SMT_solution(const Input& solution) {
    /*To set the memory types*/
    for (const auto& [mem, concreteValue] : solution.memOperand){
        if (write_dbg_memory((ea_t)mem.getAddress(), &concreteValue, mem.getSize()) != mem.getSize()) {
            msg("[!] Could not inject solution at " MEM_FORMAT "\n", (ea_t)mem.getAddress());
            continue;
        }
        tritonCtx.setConcreteMemoryValue(mem, concreteValue);

        if (cmdOptions.showExtraDebugInfo){
            char ascii_value[5] = { 0 };
            if(std::isprint(static_cast<unsigned char>(concreteValue)))
                qsnprintf(ascii_value, sizeof(ascii_value), "(%c)", static_cast<char>(concreteValue));
            std::stringstream stream;
            stream << std::hex << concreteValue;
            msg("[+] Memory " MEM_FORMAT " set with value 0x%s %s\n", 
                mem.getAddress(), 
                stream.str().c_str(), 
                std::isprint(static_cast<unsigned char>(concreteValue))? ascii_value :"");
        }
    }

    /*To set the register types*/
    for (const auto& [reg, concreteRegValue] : solution.regOperand) {
        if (!IDA_setCurrentRegisterValue(reg.getName().c_str(), static_cast<uint64>(concreteRegValue))) {
            msg("[!] Could not inject solution into register %s\n", reg.getName().c_str());
            continue;
        }
        tritonCtx.setConcreteRegisterValue(reg, concreteRegValue);

        if (cmdOptions.showExtraDebugInfo) {
            char ascii_value[5] = { 0 };
            if (std::isprint(static_cast<unsigned char>(concreteRegValue)))
                qsnprintf(ascii_value, sizeof(ascii_value), "(%c)", static_cast<char>(concreteRegValue));
            std::stringstream stream;
            stream << std::hex << concreteRegValue;

            msg("[+] Registers %s set with value 0x%s %s\n", 
                reg.getName().c_str(), 
                stream.str().c_str(),
                std::isprint(static_cast<unsigned char>(concreteRegValue)) ? ascii_value : "");
        }
    }

    if (cmdOptions.showDebugInfo)
        msg("[+] Memory/Registers set with the SMT results\n");
}


void negate_inject_maybe_restore_solver(ea_t pc, int path_constraint_index, bool restore) {
    auto solutions = solve_formula(pc, path_constraint_index);

    Input* chosen_solution = nullptr;
    if (solutions.size() > 0) {
        if (solutions.size() == 1) {
            chosen_solution = &solutions[0];
            triton::ast::SharedAbstractNode new_constraint;
            for (auto& [taken, srcAddr, dstAddr, constraint] : tritonCtx.getPathConstraints().back().getBranchConstraints()) {
                // Let's look for the constraint we have force to take wich is the a priori not taken one
                if (!taken) {
                    new_constraint = constraint;
                    break;
                }
            }
            // Once found we first pop the last path constraint
            tritonCtx.popPathConstraint();
            // And replace it for the found previously
            tritonCtx.pushPathConstraint(new_constraint);
        }
        else {
            msg("[!] Multiple branch solutions; injection requires choosing one\n");
            return;
        }
        // Restoring re-executes the branch, so the saved flags must be retained.
        if (!restore)
            negate_flag_condition(ponce_runtime_status.last_triton_instruction);
        if (restore && !snapshot.restoreSnapshot())
            return;
        set_SMT_solution(*chosen_solution);
    }
}
