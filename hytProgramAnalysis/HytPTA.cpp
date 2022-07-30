//
//
// Created by HaveYouTall
// 2022/07/22
// Pointer Analysis
//


#include "llvm/IR/CFG.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

#include <map>
#include <string>
#include <vector>
#include <queue>
#include <stdint.h>

using std::map;
using std::pair;
using std::endl;
using std::string;
using std::vector;
using std::queue;
using namespace llvm;



#define DEBUG_TYPE "hytProgramAnalysis_PointerAnalysis"
#define SHOW_INFO  //Show fact information and result. 

namespace {
  // hytProgramAnalysis.
  struct hytProgramAnalysis : public ModulePass  { //public FunctionPass {
    static char ID; // Pass identification, replacement for typeid
    hytProgramAnalysis() : ModulePass(ID) {}

    map<Value*, size_t> Obj_; // mapping from Value obj to it position in bitvector.
    size_t objCount_;

    map<string, vector<string>> vTable_;
    queue<pair<Value*, BitVector>> WL_; // Work List.
    vector<Function*> RM_; // Reachable method.
    map<string, vector<pair<size_t, string>>> CG_; // Call Graph. E.g., FuncA -> <callSiteB, FuncB>, <callSiteC, FuncC>
    map<Value*, BitVector>PT_; //PointerSet for each variable v (Value).
    map< Value*, vector<Value*> > PFG_; //

    void PrintObj() {
      errs() << "  \t[" << "\033[34m" << "*" << "\033[0m" << "]" 
             << " Obj => idx\n" ;
      for(map<Value*, size_t>::iterator obj = Obj_.begin(); obj != Obj_.end(); obj++) {
        errs() << "\t" << *(obj->first) << " => " << obj->second << "\n";
      }
    }

    void PrintCG() {
      errs() << "  \t[" << "\033[34m" << "*" << "\033[0m" << "]" 
             << " Call Graph: caller => <callsite, callee>\n" ;
      for(map<string, vector<pair<size_t, string>>>::iterator caller = CG_.begin(); caller != CG_.end(); caller++) {
        errs() << "\t" << caller->first << " => ";
        for(vector<pair<size_t, string>>::iterator callee = caller->second.begin(); callee != caller->second.end(); callee++) {
          errs() << "<" << callee->first << ", " << callee->second << ">, ";
        }
        errs() << "\n";
      }
    }

    void PrintRM() {
      errs() << "  \t[" << "\033[34m" << "*" << "\033[0m" << "]" 
             << " Reachable Method: \n" ;
      errs() << "\t" ;
      for(Function *F : RM_) {
        errs() << F->getName() << ", ";
      }
      errs() << "\n";
    }

    void PrintPT() {
      errs() << "  \t[" << "\033[34m" << "*" << "\033[0m" << "]" 
             << " Pointer Set: \n" ;
      for(map<Value*, BitVector>::iterator it = PT_.begin(); it != PT_.end(); it++) {
        errs() << "\t" <<*(it->first) << " : ";
        PrintBitVector(it->second);
      }
    }

    void PrintPFG() {
      errs() << "  \t[" << "\033[34m" << "*" << "\033[0m" << "]" 
             << " PFG: obj => obj \n" ;
      for(map< Value*, vector<Value*> >::iterator source = PFG_.begin(); source != PFG_.end(); source++) {
        errs() << "\t" <<*(source->first) << " => ";
        for(vector<Value*>::iterator target = source->second.begin(); target != source->second.end(); target++) {
          errs() << *(*target) << ", " ;
        }
        errs() << "\n";
      }
    }

    void PrintVTable(map<string, vector<string>> vTable) {
      errs() << "  [" << "\033[34m" << "*" << "\033[0m" << "]" 
             << " vTable => Contents:\n" ;
      for(map<string, vector<string>>::iterator it = vTable.begin();
          it != vTable.end(); it++) {
            errs() << (it->first) << " => ";
            for(vector<string>::iterator i = it->second.begin(); i != it->second.end(); i++) {
              errs() << *i << ", ";
            }
            errs() << "\n";
        
      }
    }


    void PrintBitVector(BitVector bit_vector) {
      for(int i = 0; i < bit_vector.size(); i++) {
        errs() << bit_vector[i] << " ";
      }
      errs() << "\n";
    }

    // res = a - b, where 1-1 = 0, 0-1 = 0. 0-0 = 0, 1-0 = 1
    void BitVectorSub(BitVector &a, BitVector &b, BitVector &res) {
      if(a.size() != b.size()) {
        errs() << "\033[31m" << "[ERROR] BitVectorSub error -- Operands size are not equal!\n" << "\033[0m";
        exit(-1);
      }
      if(res.size() != a.size()) {
        res = BitVector(a.size(), false);
      }
      for(size_t i = 0;i < a.size();i++) {
        // if and only if a[i] == true, b[i] == false, then a[i] - b[i] = 1
        if(!b[i] && a[i]) { 
            res[i] = true;
        } else {
          res[i] = false;
        }
      }
    }

    string ConvertValueToString(Value *v) {
      string buffer;
      raw_string_ostream ostream(buffer);
      ostream << *v;
      return ostream.str();
    }




    // Find obj that contains this method call
    /// @param indirctCall, the Instruction obj of indirect call.
    /// @return obj, the obj that contains this indirect call.
    Value* FindMethodCallObj(Instruction *indirctCall, int &offset) {
      Value *v;
      Instruction *inst = indirctCall;
      string opcodeName;
      offset = -1;
      do {
        v = inst->getOperand(0);
        inst = dyn_cast<Instruction>(v);
        opcodeName = inst->getOpcodeName();
        if(opcodeName == "getelementptr") {
          offset = std::stoi(inst->getOperand(1)->getNameOrAsOperand()); // Get the function offset in vtable.
        }
      }while(!(opcodeName == "alloca"));

      return v;
    }

    Value* FindCallObj(Value *obj) {
      for (User *U : obj->users()) { // Find where uesd this value obj.
        if(StoreInst *store = dyn_cast<StoreInst>(U)) { // If find the store opcode.
          // errs() << "find store: " << *store << "\n";
          obj = store->getOperand(0); // A direct call obj (e.g. %call), or maybe a bitcast instruction.
          if(dyn_cast<BitCastInst>(obj)) {
            break;
          } else {
            continue;
          }
        }
      }

      return obj;
    }

    Function* FindCall(Value *obj) {
      Function *F = NULL;
      for (User *U : obj->users()) {
        if(CallInst *call = dyn_cast<CallInst>(U)) {
          F = call->getCalledFunction();
          break;
        }
        if (InvokeInst *call = dyn_cast<InvokeInst>(U)) {
          F = call->getCalledFunction();
          // errs() << *F << "\n";
          break;
        }
      }
      return F;
    }

    // Dispatch a indirect call.
    string Dispatch(Value *x, Value *oi, Instruction *indirctCall) {
      Function *F;
      int offset; // Function offset in vtable.
      Value *obj = FindMethodCallObj(indirctCall, offset); // Find the obj that contains this indirect call
      // errs() << "Find obj: " << *obj << " with offset: " << offset <<"\n";
      if(x != obj) { // Not x.foo(), than exit to find next call.
        return "";
      }

      // errs() << "obj = x\n";
      obj = FindCallObj(obj); // Find exact call obj (e.g. %call) or a bitcast instruction.
      // errs() << "obj new: " << *obj << "\n";
      // Find the exact call that produce THIS call obj.
      while(!(F = FindCall(obj))) { 
      // If can't find call immediately (because the obj is still not the call obj, but a bitcast instrction), 
      // than we need find Find it again.
      // After FindCall method, obj will be upgraded to the param used by Obj.constructor 
      // or the param that points to the other param used by Obj.constructor.
        obj = (dyn_cast<Instruction>(obj))->getOperand(0); // Find the param that obj points to, if obj is not used by obj.constructor.
      }

      // errs() << "Final obj: " << *obj << "\n";
      if(oi == obj) { // If this function belongs to oi, then dispatch it.
        string vTable = "";
        bool alreadyFound = false;
        for (Function::iterator bb = F->begin(); bb != F->end(); ++bb) {        
          for (BasicBlock::iterator inst = bb->begin(); inst != bb->end(); ++inst) {
            if(dyn_cast<StoreInst>(inst)) { // If it is a store instruction.
              
              vTable = ConvertValueToString(inst->getOperand(0));

              if(vTable.find("getelementptr") != string::npos) { // Check if this store operation is vtable setting. 
                size_t start = vTable.find("@") + 1;
                size_t len = vTable.substr(start).find(",");
                vTable = vTable.substr(start, len);     
                // errs() << vTable << "\n";  
                alreadyFound = true; // Skip the rest search, otherwise vTable will be changed to incorrect ans.
                break;   
              }
            }
          }
          if(alreadyFound) {// Skip the rest search, otherwise vTable will be changed to incorrect ans.
            break;
          }
        }

        if(offset==-1) { // If offset is invaild.
          errs() << "[ERROR] Dispatch offset error. Offset: " << offset << ".\n" ;
          exit(-1);
        } else {
          if(vTable_.find(vTable) == vTable_.end()) { // If we do not find the vTable.
            errs() << "[ERROR] Dispatch vTable error. Table name: " << vTable << ".\n" ;
            exit(-1);
          }
          return vTable_[vTable][offset];
        }
      } else {
        return ""; // This indirect call does not belong to oi.
      }

      
    }

    // AddEdge from s to t.
    void AddEdge(Value *s, Value *t) {
      bool isExist = false;
      if(PFG_.find(s) == PFG_.end()) { // No s -> t, because there is no edges that coming out from s.
        vector<Value*> tmp;
        tmp.push_back(t);
        PFG_.insert( pair<Value*, vector<Value*>>(s, tmp) );
        if(PT_.find(s) == PT_.end()) {
          BitVector tmp = BitVector(objCount_, false);
          PT_.insert(pair<Value*, BitVector>(s, tmp));
        }
      } else { // There exist edges that coming out from s.
        // Find if exist s -> t.
        for(size_t idx = 0; idx < PFG_[s].size(); idx++) {
          if(PFG_[s][idx] == t) {
            isExist = true;
            break;
          }
        }
        if(!isExist) { // No s -> t
          PFG_[s].push_back(t);
        }
      }

      if(!isExist && (PT_[s].any())) { // If no s->t is set before, and pt(s) is not empty.
        if(PT_[s].size() < objCount_) { // Resize BitVector at everytime we using it.
          PT_[s].resize(objCount_);
        }
        WL_.push(pair<Value*, BitVector>(t, PT_[s])); // add <t, pt(s)> to WL.
      }
    }

    // Propagate the pointer set.
    void Propagate(Value *n, BitVector pts) {
      if(pts.any()) { // If not empty.
        if(pts.size() < PT_[n].size()) {
          pts.resize(PT_[n].size());
        } else if (pts.size() < PT_[n].size()) {
          PT_[n].resize(pts.size());
        }
        PT_[n] |= pts;

        if(PFG_.find(n) != PFG_.end()) {
          // for(size_t idx; idx < PFG_[n].size(); idx++) { 
          for(Value *s : PFG_[n]) { // Foreach n -> s in PFG.
            WL_.push(pair<Value*, BitVector>(s, pts)); // add <s, pts> to WL.
          }
        }
      }
    }

    // void FindVaraiable(Value *v) {
    //   // for (User *U : v->users()) {
        
    //   // }
    //   errs() << "Value: " << *v << "\n";
    //   User *U = v->user_back();
    //   errs() << "User: " << *U << "\n";
    // }

    // 
    void AddReachable(Function *F) {
      bool isReachable = false;
      for(size_t idx = 0; idx < RM_.size(); idx++) {
        if(RM_[idx] == F) {
          isReachable = true;
          break;
        }
      }

      if(!isReachable) {
        // errs() << "Add reachable to func really: " << F->getName() << "\n"; 
        RM_.push_back(F);
        for (Function::iterator bb = F->begin(); bb != F->end(); ++bb) {        
          for (BasicBlock::iterator inst = bb->begin(); inst != bb->end(); ++inst) {
            string opcodeName = inst->getOpcodeName();
            if(opcodeName == "store") { // It has already contains x = new T(), and x = y.
              Value *leftV = inst->getOperand(1);
              Value *rightV = inst->getOperand(0);
              //string leftOperandName = dyn_cast<Instruction>(leftV)->getOpcodeName(); 
              // If the second operand is alloca, than it is a assign operation.
              // errs() << "left: " << *leftV << "\n";
              // errs() << "right: " << *rightV << "\n";
              //if(leftOperandName == "alloca" && dyn_cast<Instruction>(rightV)) { 
              if(dyn_cast<AllocaInst>(leftV) && dyn_cast<Instruction>(rightV)) { // Find assign leftV = rightV, where rightV is not a constant.
                // errs() << "process obj assign thing.\n";
                if(ConvertValueToString(leftV).find("*,") != string::npos) { // If leftV is a pointer obj.
                  // errs() << "It is a assign to an obj.\n";
                  if(LoadInst *ldinst = dyn_cast<LoadInst>(rightV)) { // case: x = y or x = z.foo(); i.e., %tmp = load y; store %tmp, x;
                    // errs() << "\t case: x = y \n";
                    //string opcodeName = dyn_cast<Instruction>(ldinst->getOperand(0))->getOpcodeName();
                    //if(opcodeName == "alloc") { 
                    if(dyn_cast<AllocaInst>(ldinst->getOperand(0))) { // case: x = y, instead of x = z.foo();
                      // errs() << "Add edge: " << *rightV << " => " << *leftV << "\n";
                      // AddEdge(rightV, leftV);
                      AddEdge(ldinst->getOperand(0), leftV);
                    } 
  
                  } else {  // With bug ///////////////////////////////
                    if( Instruction *rightInst = dyn_cast<Instruction>(rightV)) {
                      if(rightInst->isUnaryOp()  || // If it is Unary Operations.
                        rightInst->isBinaryOp() || // If it is Binary Ops.
                        rightInst->isShift()    || // If it is Shift Ops.
                        rightInst->isBitwiseLogicOp()) {
                        /// TODO: when it comes to objA = objB + objC; where + operator is override by class.
                        errs() << "[Warning] Now not support analysis for objA = objB op objC. The inst is: " 
                              << *leftV << " '=' " << *rightV << "\n";
                      } else if (!dyn_cast<CallInst>(rightV)) { // case: x = new T();, not a call like x = a.foo();
                        // errs() << "\t case: x = new T() \n";
                        // Confirm obj.
                        Value *obj = rightV;
                        while(!dyn_cast<CallInst>(dyn_cast<Instruction>(obj)->getOperand(0))) {
                          obj = dyn_cast<Instruction>(obj)->getOperand(0);
                        }

                        if(Obj_.find(obj) == Obj_.end()) {
                          Obj_.insert(pair<Value*, size_t>(obj, objCount_++));
                        }
                        BitVector tmp = BitVector(objCount_, false);
                        tmp[Obj_[obj]] = true;
                        
                        // Confirm x.
                        Value *x = NULL;
                        for(User *U : rightV->users()) {
                          if(StoreInst *store = dyn_cast<StoreInst>(U)) {
                            x = store->getOperand(1);
                            if(dyn_cast<AllocaInst>(x)) {
                              break;
                            } else {
                              x = NULL;
                            }
                          }
                        }
                        // WL_.push(pair<Value*, BitVector>(rightV, tmp));
                        WL_.push(pair<Value*, BitVector>(x, tmp));
                        // errs() << "Add WL: " << *x << " => " << *rightV << "\n";
                      } else {
                        if(dyn_cast<CallInst>(rightV)) { // When there is x = a.foo(); Skip it, because we will handle this when perform ProcessCall.
                          continue;
                        }
                        /// TODO: when it comes to int *p; int a = 0; p = &a;
                        errs() << "[Warning] Now not support analysis for int *p; int a = 0; p = &a;. The inst is: " 
                              << *leftV << " '=' " << *rightV << "\n";
                      }
                        
                    } 
                    
                  }
                }
              }
            } 
            
            /*else if (opcodeName == "call") {
              errs() << *inst << "\n";
              // string calledFunc = dyn_cast<CallInst>(inst)->getCalledFunction()->getName().data();
              // if(calledFunc == "_Znwm") { // If it is a new operation.
                
              //   Value *v = dyn_cast<Value>(inst);
              //   errs() << *v << "\n";
              // }
              string calledFunc = ConvertValueToString(dyn_cast<Value>(inst));
              // errs() << calledFunc << "\n";
              if(calledFunc.find("_Znwm") != string::npos) {
                errs() << "it is new: " << calledFunc << "\n";
                /// TODO: add something to WL_
                FindVaraiable(dyn_cast<Value>(inst));
              }
            }
            */
          }
        }
      }
      // string funcName = F->getName().data();
      // errs() << "current func name: " << funcName << "\n";
    }

    string ExtractObjClass(Value *v) {
      string objClass = "";
      string objValue = ConvertValueToString(v);
      size_t start = objValue.find("\%class");
      if(start != string::npos) {
        if(dyn_cast<AllocaInst>(v)) { 
          size_t len = objValue.find(", align") - (start + 1);
          objClass = objValue.substr(start + 1, len);
        }
        if(dyn_cast<BitCastInst>(v)) { 
          objClass = objValue.substr(start + 1); 
        }
        if(dyn_cast<GetElementPtrInst>(v)) {
          size_t len = objValue.find(", ") - (start + 1);
          objClass = objValue.substr(start + 1, len);
        }
      } else {
        errs() << "[ERROR] Extract Object Class error when process: ' " << *v << " '.\n";
        exit(-3);
      }
      return objClass;
    }

    // Parsing the idx argument when callInst happened.
    Value* ParsingArgument(CallInst *call, size_t idx) {
      Value *res = call->getOperand(idx);
      while(!dyn_cast<AllocaInst>(res)) {
        Instruction *inst = dyn_cast<Instruction>(res);
        if(dyn_cast<LoadInst>(res)) {
          res = inst->getOperand(0);
        } else if(inst->isUnaryOp()  || // If it is Unary Operations.
                  inst->isBinaryOp() || // If it is Binary Ops.
                  inst->isShift()    || // If it is Shift Ops.
                  inst->isBitwiseLogicOp()) {
          // Obj_.insert(pair<Value*, size_t>(res, objCount_++));
          /// TODO: when it comes to a = b op c, where a is the argument when call inst happend.
          ///       We need to futher process this situation. Here the res should be both b and c, when b and c are not constant.
          ///       And AddEdge from b to param at idx, and AddEdge from c to param at idx. If b and c are both class* (Now not support like int*).
          errs() << "[Warning] Now not support analysis for a = b op c as argument to parse. The inst is: " 
                 << *inst << "\n";
          return res;
        }
      }
      return res;
      
    }

    void ProcessCall(Module &M, Value *x, Value *oi) {
      vector<Function*>tmp = RM_; // RM_ will be changed all the time. It will make the following loop go wrong.
      for(Function *F : tmp) {
        size_t callSite = 0;
        // PrintRM();
        // errs() << "\t\t\t\t\tnow process: " << *F  << "\n";
        
        for (Function::iterator bb = F->begin(); bb != F->end(); ++bb) {        
          for (BasicBlock::iterator inst = bb->begin(); inst != bb->end(); ++inst) {
            callSite++;
            Function *func = NULL; // Store the callSite callee function.
            string callee;
            // errs() << "now process inst: " << *inst << "\n";
            if(CallInst *call = dyn_cast<CallInst>(inst)) {
              // errs() << "It is a call: " << *inst << "\n";
              if(call->isIndirectCall()) { // If it is indirect call (i.e., a tmp variable from load instrustion.)
                // errs() << "\t[+] Indirect call" << "\n";
                Value *virtual_call_name = call->getOperand(call->getNumOperands() - 1); // The last operand is the virtual call name.
                Instruction *virtual_call_inst = dyn_cast<Instruction>(virtual_call_name); // It must be a load inst.

                // errs() << "\t\t" << *virtual_call_inst << "\n";
                // errs() << "\t\t" << *oi << "\n";
                callee = Dispatch(x, oi, virtual_call_inst); // Dispatch the virtual call.
                if(callee != "") {
                  // errs() << "\t\t[*]Find function: " << callee << "\n"; 
                  func = M.getFunction(callee);
                } else {
                  continue;
                }
              } else { // If it is direct call, then we can get its function name immediately.
                // errs() << "\t[+] Direct call" << *call <<"\n";
                func = call->getCalledFunction();
                // errs() << "\t\tcalled func: " << *func << "\n";
                
                // If we can get instruction, then it is not the library function.
                if(func->begin() != func->end()) {
                  // errs() << "\t\tDirect call process\n";
                  Instruction *firstInst = dyn_cast<Instruction>(func->begin()->begin());
                  string firstInstClass = ExtractObjClass(firstInst); // Find dircet call's class.
                  string oiClass = ExtractObjClass(oi); // Find obj oi's class.
                  if(oiClass == firstInstClass) { // If direct call belongs to oi object.
                    callee = func->getName().data();
                    // errs() << "\t\t[*] Direct call name is: " << callee << "\n";
                  } else {
                    continue;
                  }
                } else { // Otherwise it is a library function, we will not process it.
                  func = NULL;
                }

                // errs() << "\t\tfinish this call process\n";
                
              }

              if(func) { // Dispatch success.
                // errs() << "dispatch func: " << func->getName() << "\n";
                // Process this obj.
                BitVector tmp = BitVector(objCount_, false);
                tmp[Obj_[oi]] = true;
                WL_.push(pair<Value*, BitVector>(dyn_cast<Instruction>(func->begin()->begin()), tmp)); // add <mthis, {oi}> to WL.
                string caller = F->getName().data();
                bool isExist = false;
                if(CG_.find(caller) != CG_.end()) { // If this caller's CG exists, then check if caller -> <callSite, callee> exists.
                  for(pair<size_t, string> target : CG_[caller]) {
                    if(target.first == callSite && target.second == callee) {
                      isExist = true;
                      break;
                    }
                  }
                  if(!isExist) { // If caller -> <callSite, callee> does not exist, than add it. 
                    CG_[caller].push_back(pair<size_t, string>(callSite, callee));
                  }
                } else { // If this caller's CG does not exist, than add it.
                  vector<pair<size_t, string>>tmp;
                  tmp.push_back(pair<size_t, string>(callSite, callee));
                  CG_.insert(pair<string, vector<pair<size_t, string>>>(caller, tmp));
                }

                if(!isExist) { // If not caller -> <callSite, callee> exist.
                  // errs() << "Add reachable to func: " << func->getName() << "\n"; 
                  AddReachable(func);

                  // Process params and arguments.
                  size_t idx = 0;
                  for (Argument &Arg : func->args()) { // Get function's params.
                    if(idx > 0) { // Skip param: this, because we already process before.
                      // errs() <<  Arg << " : ";
                      Value *param = &Arg;
                      /// TODO: Should we process if param is a pointer obj? i.e., if param is like int *p ?
                      if(ConvertValueToString(param).find("*, ") != string::npos) { // If param is a pointer.
                        Value *ai = ParsingArgument(call, idx);
                        if (!dyn_cast<Constant>(ai)) {
                          errs() << func->getName() << " argument: " << *ai << "\n";
                          AddEdge(ai, param);
                        } else {
                          errs() << func->getName() << " constant argument: " << *ai << "\n";
                          continue;
                        }
                      }
                      
                      
                    }
                    idx++;
                  }
                   

                  // Process the ret value propagate. 
                  //if(func->getReturnType()->getTypeID() != Type::VoidTyID ) { // Not void.
                  if(func->getReturnType()->getTypeID() == Type::PointerTyID ) { // If it is a pointer.
                    // Process the ret value in the call site.
                    Value *callValue = dyn_cast<Value>(inst);
                    // errs() << "Now process ret value: " << *callValue << "\n";
                    Value *r = NULL;
                    for(User *U : callValue->users()) {
                      if(StoreInst *store = dyn_cast<StoreInst>(U)) {
                        r = store->getOperand(1);
                        break;
                      } else if(dyn_cast<BitCastInst>(U)) {
                        User *tmp = U->user_back();
                        if(StoreInst *store = dyn_cast<StoreInst>(U)) {
                          r = store->getOperand(1);
                          break;
                        } else {
                          errs() << "[ERROR] Unhandled exception when get the ret value. The inst is: " << *inst << "\n";
                          exit(-4);
                        }
                      }
                    }
                    if(r == NULL) {
                      errs() << "[ERROR] Unhandled exception when get the ret value. The inst is: " << *inst << "\n";
                      exit(-4);
                    }

                    // errs() << "return value at call site: " << *r << "\n";


                    // Find all ret in func (callee).
                    for (Function::iterator bb = func->begin(); bb != func->end(); ++bb) {        
                      for (BasicBlock::iterator inst = bb->begin(); inst != bb->end(); ++inst) {
                        if(ReturnInst *mret = dyn_cast<ReturnInst>(inst)) {
                          Value *mretValue = mret->getReturnValue();
                          if(LoadInst *load = dyn_cast<LoadInst>(mretValue)) {
                            mretValue = load->getOperand(0);
                            // errs() << "return value in callee: " << *mretValue << "\n";
                            AddEdge(mretValue, r);
                          } else if(Constant *c = dyn_cast<Constant>(mretValue)) {
                            errs() << "[INFO] Return a constant, no pointer analysis needed here: " << *mret <<"\n";
                          } else {
                            errs() << "[ERROR] Unhandled exception when get the ret value in callee. The inst is: " << *mret 
                                   << "  ->  " << *mretValue << "\n";
                            exit(-4);
                          }
                        }
                      }
                    }
                  }

                } else { 

                }
              }
              // errs() << "next call\n";
            }
          }
        }
        // errs() << "Next func\n";//*/
      }
      // errs() << "finish process call\n";
    }


    // void testResize() {
    //   BitVector a = BitVector(6, false);
    //   BitVector b = BitVector(12, false);

    //   a[0] = true;a[2] = true;a[4] = true;a[5] = true;

    //   PrintBitVector(a);
    //   a.resize(b.size());
    //   PrintBitVector(a);
    // }


    // Entry of Pointer Analysis.
    bool runOnModule(Module &M) override {

      // Analysis the vTable structure.
      for (auto gv_iter = M.global_begin();gv_iter != M.global_end(); gv_iter++) {
        /* GLOBAL DATA INFO*/
        GlobalVariable *gv = &*gv_iter;
        // errs() << *gv << "\n";

        string vTable = ConvertValueToString(gv); // GlobalVariable is one subclass of Value. 

        if(vTable.find("linkonce_odr dso_local unnamed_addr constant") != string::npos) { // If it is a vTable name.
          vector<string> tmpvTable;
          // errs() << *gv << "\n";
          size_t start = 1;
          size_t len = vTable.find(" =") - start;
          string vTable_name = vTable.substr(start, len); // Get the vTable name.
          // errs() << "\tname: " << vTable_name << "\n";
          string rest = vTable.substr(start + len);
          rest = rest.substr(rest.find(" to") + 3);
          // errs() << "\t" << rest << "\n";
          while((start = rest.find("@"))!=string::npos) { // Find all function in this vTable.
            len = rest.find(" to") - (start + 1);
            // errs() << "\tlen: " << len << "\n";
            // string res = rest.substr(start+1, len);
            // errs() << "\tcontent: " << res << "\n";
            tmpvTable.push_back(rest.substr(start+1, len));
            rest = rest.substr(start + 1 + len + 3);
            // errs() << "\trest: " << rest << "\n";
            // break;
          }
          vTable_.insert( pair<string, vector<string>>(vTable_name, tmpvTable) );
        }
        
      }
#ifdef SHOW_INFO
      PrintVTable(vTable_);
#endif

      Function *F = M.getFunction("main");
      if (!F) {
        errs() << "[ERROR] Main Function Not Found! Analysis is stopped\n";
        exit(-2);
      }

      // errs() << F->getName() << "'s params: " ; // Get the params of a function.
      // for (Argument &Arg : F->args()) {
      //   // Value *ArgVal = call->getArgOperand(Arg.getArgNo());
      //   errs() <<  Arg << " : ";
      // }
      // errs() << "\n";

      // DoInit(M, F);

#ifndef SHOW_INFO
      for (Function::iterator bb = F->begin(); bb != F->end(); ++bb) {        
        for (BasicBlock::iterator inst = bb->begin(); inst != bb->end(); ++inst) {
          string instStr = ConvertValueToString(dyn_cast<Value>(inst));
          if(instStr.find("getelementptr inbounds") != string::npos) {
            errs() << *inst << " : " << inst->getNumOperands() << "\n";
          }
        }
      }
#endif 

#ifndef DEBUG
      AddReachable(F);
      while(!WL_.empty()) {
        pair<Value*, BitVector> working = WL_.front(); // working = <n, pts>
        WL_.pop();
        BitVector delta = BitVector(objCount_, false);

        // PrintObj(); 
        // errs() << "  [" << "\033[34m" << "+" << "\033[0m" << "]" 
        //        << " Now process: " << *(working.first) << " ";
        // errs() << "With pts: ";
        PrintBitVector(working.second);

        if(PT_.find(working.first) == PT_.end()) {
          BitVector tmp = BitVector(objCount_, false);
          PT_.insert(pair<Value*, BitVector>(working.first, tmp));
        }

        if(working.second.size() < objCount_) {
          working.second.resize(objCount_);
        }

        if(PT_[working.first].size() < objCount_) {
          PT_[working.first].resize(objCount_);
        }

        BitVectorSub(working.second, PT_[working.first], delta); // delta = pts - pt[n]

        Propagate(working.first, delta);

        if(dyn_cast<AllocaInst>(working.first)) { // If n is a variable x.
          for(size_t idx = 0; idx < delta.size(); idx++) { // Foreach oi in delta.
            //First find oi.
            if(delta[idx]) {
              Value *oi;
              for(map<Value*, size_t>::iterator it = Obj_.begin(); it != Obj_.end(); it++) {
                if(it->second == idx) {
                  oi = it->first;
                  break;
                }
              }
              
              // errs() << "Current obj: " << *oi << "\n";

              // Find y = x.f or x.f = y
              for(Function *F : RM_) {
                size_t callSite = 0;
                for (Function::iterator bb = F->begin(); bb != F->end(); ++bb) {        
                  for (BasicBlock::iterator inst = bb->begin(); inst != bb->end(); ++inst) {
                    if(StoreInst *store = dyn_cast<StoreInst>(inst)) {
                      // errs() << "process store inst: " << *store << "\n";
                      Value *leftV = store->getOperand(1);
                      Value *rightV = store->getOperand(0);
                      Value *fieldV = NULL;

                      
                      if(dyn_cast<AllocaInst>(leftV)) { // Maybe y = x.f
                        fieldV = rightV;
                      } else if(dyn_cast<AllocaInst>(rightV)) { // Mayby x.f = y
                        fieldV = leftV;
                      } else {
                        continue;
                      }

                      Instruction *fieldInst = dyn_cast<Instruction>(fieldV);
                      string fieldVStr = ConvertValueToString(fieldV);
                      if(fieldVStr.find("getelementptr inbounds") != string::npos && fieldInst->getNumOperands() == 3) {
                        string fieldObjClass = ExtractObjClass(fieldV);
                        string oiClass = ExtractObjClass(oi);
                        // errs() << "fieldV: " << *fieldV << "\n";
                        // errs() << "oi: " << *oi << "\n";
                        if(oiClass == fieldObjClass) { // if oi and x.f belong to the same class, than x.f here is oi.f
                          AddEdge(rightV, leftV); // add oi.f -> y or x -> oi.f
                        }
                      }
                    }
                  }
                }
              }

              // ProcessCall(x, oi)
              // errs() << "process call\n";
              // PrintRM();
              ProcessCall(M, working.first, oi);

            }
          }
        }
      } // while()

      errs() << "  [" << "\033[34m" << "+" << "\033[0m" << "]"
             << " Final Result: \n";
      PrintObj();
      PrintCG();
      PrintRM();
      PrintPT();
      PrintPFG();


#endif
      errs() << "[" << "\033[32m" << "+" << "\033[0m" << "]"
             << " Finised\n";

      return false;

    }
  };
}

char hytProgramAnalysis::ID = 0;
static RegisterPass<hytProgramAnalysis> X("hytDFA_PointerAnalysis", "Hyt Program Analysis For Pointer Analysis");