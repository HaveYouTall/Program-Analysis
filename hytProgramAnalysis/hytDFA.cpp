//
//
// Created by HaveYouTall
// 2022/05/21
// Reaching Definition Analysis
//


#include "llvm/IR/CFG.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/BitVector.h"

#include <map>
#include <string>
#include <stdint.h>

using std::map;
using std::pair;
using std::endl;
using std::string;
using namespace llvm;



#define DEBUG_TYPE "hytProgramAnalysis_ReachingDefinition"
#define SHOW_INFO  //Show fact information and result. 

namespace {
  // hytProgramAnalysis.
  struct hytProgramAnalysis : public FunctionPass {
    static char ID; // Pass identification, replacement for typeid
    hytProgramAnalysis() : FunctionPass(ID) {}

    // `fact_` illustrate mapping form definition instruction to its index.
    // e.g. d0: x = 1 + m; 
    //      d1: y = 1;
    // Then mapping is: x = 1 + m -> 0
    //                  y = 1 -> 1 
    map<Instruction*, uint32_t> fact_; 
    // Number of definition instruction.
    uint32_t def_count_ = 0;
    
    // `fact_mask_` illustrate mapping form varables to their masks.
    // A mask shows which definition instruction defines a specific varable.
    // e.g. d0: x = 1 + m;
    //      d1: y = 1;
    //      d2: x = 2 * y;
    //      d3: y = z + 5;
    //      d4: z = 2;
    // So, for varable x, its mask is 10100
    //     for varable y, its mask is 01010
    //     for varable z, its mask is 00001 
    map<Instruction*, BitVector> fact_mask_;
    bool is_anyone_changed_ = false;
    
    // gen and kill vevtor for each basic block.
    map<BasicBlock*, BitVector> genB_;
    map<BasicBlock*, BitVector> killB_;

    // map<BasicBlock*, BitVector> inB_;
    map<BasicBlock*, BitVector> outB_;

    void PrintFact(map<Instruction*, uint32_t> fact) {
      errs() << "  [" << "\033[34m" << "*" << "\033[0m" << "]" 
             << " Fact => index:\n" ;
      for(map<Instruction*, uint32_t>::iterator it = fact.begin();
          it != fact.end(); it++) {
        errs() << *(it->first) << " => " << it->second << '\n';
      }
    }

    void PrintFactMask(map<Instruction*, BitVector> fact_mask) {
      errs() << "  [" << "\033[34m" << "*" << "\033[0m" << "]" 
             << " Varable => Maks vector:\n" ;
      for(map<Instruction*, BitVector>::iterator it = fact_mask.begin();
          it != fact_mask.end(); it++) {
        errs() << *(it->first) << " => ";
        PrintBitVector(it->second);
      }
    }
    
    void PrintBitVector(BitVector bit_vector) {
      for(int i = 0; i < bit_vector.size(); i++) {
        errs() << bit_vector[i] << " ";
      }
      errs() << "\n";
    }

    // @return true if this analysis is forward, otherwise false.
    bool IsForward() {
      return true;
    }

    bool InitGenAndMask(Function *F) {
      errs() << "[" << "\033[32m" << "+" << "\033[0m" << "]" 
             << " Start to init Gen and Fact Mask with " 
             << def_count_ << " definitions\n";
      BitVector tmp_genB;
      for (Function::iterator bb = F->begin(); bb != F->end(); ++bb) {
        tmp_genB = BitVector(def_count_, false);  // Init zero vector for each block.
        for (BasicBlock::iterator inst = bb->begin(); inst != bb->end(); ++inst) {
          string inst_opcode_name = inst->getOpcodeName ();
          uint32_t index;
          if (inst_opcode_name == "store") {
            index = fact_[dyn_cast<Instruction>(inst)];
            tmp_genB[index] = true;

            // The second operand of store instruction is the left value, 
            // i.e., defined varable.
            Instruction *varable = dyn_cast<Instruction>((inst->getOperand(1)));
            if(fact_mask_.find(varable) == fact_mask_.end()) {
              fact_mask_.insert(
                pair<Instruction*, BitVector>(varable, BitVector(def_count_, false))
              );
            }
            fact_mask_[varable][index] = true;
          }
        }
        genB_.insert(pair<BasicBlock*, BitVector>(dyn_cast<BasicBlock>(bb), tmp_genB));
      }
      return true;
    }

    bool InitKill(Function *F) {
      errs() << "[" << "\033[32m" << "+" << "\033[0m" << "]" 
             <<" Start to init Kill with " 
             << def_count_ << " definitions\n";
      BitVector tmp_killB;
      for (Function::iterator bb = F->begin(); bb != F->end(); ++bb) {
        tmp_killB = BitVector(def_count_, false);  // Init zero vector for each block.
        for (BasicBlock::iterator inst = bb->begin(); inst != bb->end(); ++inst) {
          string inst_opcode_name = inst->getOpcodeName ();
          uint32_t index;
          if (inst_opcode_name == "store") {
            // The second operand of store instruction is the left value, 
            // i.e., defined varable.
            Instruction *varable = dyn_cast<Instruction>((inst->getOperand(1)));
            // Union the related definition instruction bit
            // to the varable in current definition.
            tmp_killB |= fact_mask_[varable];
          }
        }
        killB_.insert(pair<BasicBlock*, BitVector>(dyn_cast<BasicBlock>(bb), tmp_killB));
      }
      return true;
    }

    void DoInit(Function &F) {
      Function *tmp = &F;
      errs() << "[" << "\033[32m" << "+" << "\033[0m" << "]" 
             <<" Start to find all definitions and get fact set\n";
      for (Function::iterator bb = tmp->begin(); bb != tmp->end(); ++bb) {        
        for (BasicBlock::iterator inst = bb->begin(); inst != bb->end(); ++inst) {
            string inst_opcode_name = inst->getOpcodeName ();
            if (inst_opcode_name == "store") {
              fact_.insert(pair<Instruction*, uint32_t>(dyn_cast<Instruction>(inst), def_count_));
              def_count_++;
            }
        }
      }
#ifdef SHOW_INFO
      PrintFact(fact_);
#endif
      if(!InitGenAndMask(&F)) {
        errs() << "\033[31m" << "[ERROR] Initializing genB vector and varable mask failed!\n" << "\033[0m";
        exit(-1);
      }
      if(!InitKill(&F)) {
        errs() << "\033[31m" << "[ERROR] Initializing killB vector failed!\n" << "\033[0m";
        exit(-1);
      }
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

    // OUT[B] = genB U (IN[B] - killB);
    // @param bb, current basic block B
    // @param in, current in vector IN[B]
    // @param out, current out vector OUT[B]
    void TransferFunction(
        BasicBlock *bb, 
        BitVector &in,
        BitVector &out) {
      BitVector res;
      BitVectorSub(in, killB_[bb], res);
      res |= genB_[bb];
      if(res != out) {  // Changed
        is_anyone_changed_ = true;
        out = res;
      }
    }

    // res = a U b;
    void MeetInto(BitVector &a, BitVector &b, BitVector &res) {
      if(a.size() != b.size()) {
        errs() << "\033[31m" << "[ERROR] MeetInto error -- Operands size are not equal!\n" << "\033[0m";
        exit(-1);
      }
      if(res.size() != a.size()) {
        res = BitVector(a.size(), false);
      }
      BitVector tmp = a;
      tmp |= b;
      res = tmp;
    }

    // Forward analysis for reaching definition on function F
    void ForwardAnalysis(Function *F) {
      // Initializing OUT[entry] = empty;
      BitVector prev_out = BitVector(def_count_, false);
      // Traverse all basic block
      // For each block OUT[B] = empty;
      for (Function::iterator bb = F->begin(); bb != F->end(); ++bb) {
        outB_.insert(
            pair<BasicBlock*, BitVector>(
                dyn_cast<BasicBlock>(bb), 
                BitVector(def_count_, false)
            )
        );
      }
      
      // Do analysis if any out vector has changed.
#ifdef SHOW_INFO
      uint32_t round = 0; // Count travers round when do reaching difinition analysis.
#endif 
      do {
#ifdef SHOW_INFO
        //errs() << round++ << "round";
        round++;
#endif
        is_anyone_changed_ = false;  // Initialize all things are not changed.
        // IN[B] = U (for all predecessor P of B) OUT[P]; 
        // Where IN[B] here is called prev_out;
        for (Function::iterator bb = F->begin(); bb != F->end(); ++bb) {
          BasicBlock *basic_block = dyn_cast<BasicBlock>(bb);
          if (bb != F->begin()) { // Not entry, need to consider meet operation.
            for (BasicBlock *pred : predecessors(basic_block)) {
              MeetInto(prev_out, outB_[pred], prev_out);
            }
          } 
          
          // OUT[B] = genB U (IN[B] - killB);
          TransferFunction(basic_block, prev_out, outB_[basic_block]);
          prev_out.reset();
        }
      }while(is_anyone_changed_);
#ifdef SHOW_INFO
      errs() << "  [" << "\033[34m" << "*" << "\033[0m" << "]" 
             << " Total " << round << " round(s) analysis.\n";
#endif
    }


    void DoDataFlowAnalysis(Function *F) {
      if(IsForward()) {
        errs() << "[" << "\033[32m" << "+" << "\033[0m" << "]" 
              <<" Start to do forward analysis.\n";
        ForwardAnalysis(F);
      } else {
        errs() << "[WARNING] Not supported now!\n";
      }
    }

    bool runOnFunction(Function &F) override {
      DoInit(F);
      DoDataFlowAnalysis(&F);
#ifdef SHOW_INFO
      BasicBlock *basic_block;
      for (Function::iterator bb = F.begin(); bb != F.end(); ++bb) {
        basic_block = dyn_cast<BasicBlock>(bb);
        errs() << "  ========================= \n";
        errs() << "  For block: \n";
        errs() << *basic_block << "\n";
        errs() << "  Final OUT: ";
        PrintBitVector(outB_[basic_block]);
        errs() << "  ========================= \n";
      }
#endif
      errs() << "[" << "\033[32m" << "+" << "\033[0m" << "]"
             << " Finised\n";

      return false;
    }
  };
}

char hytProgramAnalysis::ID = 0;
static RegisterPass<hytProgramAnalysis> X("hytDFA_ReachingDefinition", "Hyt Program Analysis For Reaching Definition");