//===- Hello.cpp - Example code from "Writing an LLVM Pass" ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements two versions of the LLVM "Hello World" pass described
// in docs/WritingAnLLVMPass.html
//
//===----------------------------------------------------------------------===//

// #include "llvm/ADT/Statistic.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
// #include "llvm/IR/Instructions.h" 
// #include "llvm/IR/Intrinsics.h"
// #include "llvm/IR/InstIterator.h"
#include "llvm/ADT/BitVector.h"
// #include "llvm/ADT/PostOrderIterator.h"
// #include "llvm/Support/InstIterator.h" 


#include <map>
#include <string>
#include <stdint.h>
// #include <unordered_map>
// #include <unordered_set>
using std::map;
using std::pair;
using std::endl;
using std::string;
using namespace llvm;



#define DEBUG_TYPE "hytProgramAnalysis"

//STATISTIC(HelloCounter, "Counts number of functions greeted");

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
    // uint32_t basic_block_count_= 0;
    
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
    bool is_anyone_changed_;
    
    // gen and kill vevtor for each basic block.
    map<BasicBlock*, BitVector> genB_;
    map<BasicBlock*, BitVector> killB_;

    map<BasicBlock*, BitVector> inB_;
    map<BasicBlock*, BitVector> outB_;

    void PrintFact(map<Instruction*, uint32_t> fact) {
      errs() << "  [" << "\033[34m" << "*" << "\033[0m" << "]" << "Fact => index:\n" ;
      for(map<Instruction*, uint32_t>::iterator it = fact.begin();
          it != fact.end(); it++) {
        errs() << *(it->first) << " => " << it->second << '\n';
      }
    }

    void PrintFactMask(map<Instruction*, BitVector> fact_mask) {
      errs() << "  [" << "\033[34m" << "*" << "\033[0m" << "]" << "Varable => Maks vector:\n" ;
      for(map<Instruction*, BitVector>::iterator it = fact_mask.begin();
          it != fact_mask.end(); it++) {
        errs() << *(it->first) << " => ";// << it->second << '\n';
        PrintBitVector(it->second);
      }
    }
    
    void PrintBitVector(BitVector bit_vector) {
      // errs() << "Bit Vector: " ;
      for(int i = 0; i < bit_vector.size(); i++) {
        errs() << bit_vector[i] << " ";
      }
      errs() << "\n";
    }

    bool IsForward() {
      return true;
    }

    bool InitGenAndMask(Function *F) {
      errs() << "[" << "\033[32m" << "+" << "\033[0m" << "]" << "Start to init Gen and Fact Mask with " 
             << def_count_ << " definitions\n";
      BitVector tmp_genB;//, tmp_fact_mask;
      // tmp_fact_mask = BitVector(def_count_, false);
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
        // errs() << "For bb: \n" << *bb << "\n";
        // errs() << "gen vector is: \n";
        // PrintBitVector(tmp_genB);
        // PrintFactMask()
      }
#ifdef DEBUG
      PrintFactMask(fact_mask_);
#endif 
      return true;
    }
#define SHOW_INFO
    bool InitKill(Function *F) {
      errs() << "[" << "\033[32m" << "+" << "\033[0m" << "]" <<"Start to init Kill with " 
             << def_count_ << " definitions\n";
      BitVector tmp_killB;//, tmp_fact_mask;
      // tmp_fact_mask = BitVector(def_count_, false);
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
#ifdef DEBUG
        errs() << "\n\n ================================ \n";
        errs() << "For bb: \n" << *bb << "\n";
        errs() << "gen vector is :\n";
        PrintBitVector(genB_[dyn_cast<BasicBlock>(bb)]);
        errs() << "kill vector is: \n";
        PrintBitVector(tmp_killB);
        // PrintFactMask()
#endif
      }
      return true;
    }

    void DoInit(Function &F) {
      Function *tmp = &F;
      // 遍历函数中的所有基本块
      errs() << "[" << "\033[32m" << "+" << "\033[0m" << "]" <<"Start to find all definitions"
             << "and get fact set\n";
      for (Function::iterator bb = tmp->begin(); bb != tmp->end(); ++bb) {
        // 遍历基本块中的每条指令
        // errs() << "I am running on a block...\n";
        // errs() << "Block name: " << bb->getName().str() << "\n" ;//<< endl;
        // errs() << "Block content: " << *bb << "\n" ;
        // errs() << "==================================\n";
        // BasicBlock *tmpbb =bb->getSinglePredecessor();
        // if(tmpbb){
        //   errs() << "\tpreds: " << *tmpbb << "\n";
        // }
        // errs() << "==================================\n";
        
        for (BasicBlock::iterator inst = bb->begin(); inst != bb->end(); ++inst) {
            // 是否是add指令
            //errs() << "I am running in Instruction...\n";
            // if(isa<PHINode>(*inst)){
            //   errs() << "phi\n"; 
            //   errs() << *inst << "\n";
            // }

            //string ins = *inst;
            
            // errs() << "=============== inst ================\n";
            // errs() << "Inst: " << *inst << "\n";
            string inst_opcode_name = inst->getOpcodeName ();
            // errs() << "Opcode Name: " << inst_opcode_name << "\n";
            
            // if (instOpcodeName == "alloca") {
            //   errs() << "\t[+] It is alloca\n";
            //   allocaCount++;
            // } 
            if (inst_opcode_name == "store") {
              // errs() << "\t[+] It is definition\n";
              fact_.insert(pair<Instruction*, uint32_t>(dyn_cast<Instruction>(inst), def_count_));
              def_count_++;
              // // The second operand of store instruction is the left value, 
              // // i.e., defined varable.
              // Instruction *varable = dyn_cast<Instruction>((inst->getOperand(1))); 
              
              
              // errs() << tmpIns << "\n";
              // if (tmpIns){
              //   errs() << "\t[+] Has opcode name: " << tmpIns->getOpcodeName() << "\n";
              // }else {
              //   errs() << "\t[+] Other type Ins\n";
              // }
              // errs() << "\t[+] Test: " << *(dyn_cast<Instruction>(inst)) << "\n";
            }

            // errs() << "left value: " << inst->getName() << "\n";
            // for(int i =0; i< inst->	getNumOperands();i++) {
            //   errs() << "Oprand " << i << ": " << *(inst->getOperand(i))/*->getValueName()*/ << "\n";
            //   // StringRef name = 
            //   Instruction *tmpIns = dyn_cast<Instruction>((inst->getOperand(i)));//->getValueName();
            //   if (tmpIns){
            //     errs() << "\t[+] Has opcode name: " << tmpIns->getOpcodeName() << "\n";
            //   }else {
            //     errs() << "\t[+] Other type Ins\n";
            //   }
            // // }
            
            // errs() << "===============================\n";


            
            // if (inst->isBinaryOp()) {
            //     errs() << "Find OP\n"; 
            //     // if (inst->getOpcode() == Instruction::Add) {
            //     //     errs() << "Find ADD instruction\n";
            //     //     ob_add(cast<BinaryOperator>(inst));
            //     // }
            // }
        }
      }

      // Set init fact = 000..00 according to num of definition
      //fact_ = 
      //BitVector(def_count_, true);
      // BitVector test = BitVector(defCount, false);
      // test[4] = true;
      // fact &= test;
      // errs() << "defCount: " << def_count_ << "\n";
#ifdef SHOW_INFO
      PrintFact(fact_);
#endif
      // is_changed_ = BitVector(def_count_, false);
      // is_anyone_changed_ = false; // Initialize all things are not changed.
      if(!InitGenAndMask(&F)) {
        errs() << "\033[31m" << "[ERROR] Initializing genB vector and varable mask failed!\n" << "\033[0m";
        // return false;
        exit(-1);
      }
      if(!InitKill(&F)) {
        errs() << "\033[31m" << "[ERROR] Initializing killB vector failed!\n" << "\033[0m";
        // return false;
        exit(-1);
      }
      //errs() 
      // return true;
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
    void TransferFunction(
        BasicBlock *bb, 
        /*map<BasicBlock*, BitVector> in, 
        map<BasicBlock*, BitVector> out*/
        BitVector &in,
        BitVector &out) {
      BitVector res;
      BitVectorSub(in, killB_[bb], res);
      res |= genB_[bb];
      if(res != out) {  // Changed
        is_anyone_changed_ = true;  
        out = res;
      }
      // if(res != out[bb]) { // Not change
      //   is_anyone_changed_ = true;
      //   out[bb] = res;
      // }
    }


// #include <stdarg.h>
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

// #include "llvm/IR/BasicBlock.h" 

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
      uint32_t round = 1;
      do {
        is_anyone_changed_ = false;  // Initialize all things are not changed.
#ifdef DEBUG
        errs() << "************ " << round++ << " round ****************\n";
#endif
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
#ifdef DEBUG
          errs() << " ================= \n";
          errs() << "For block: \n";
          errs() << *basic_block << "\n";
          errs() << "IN: ";
          PrintBitVector(prev_out);
          errs() << "OUT: ";
          PrintBitVector(outB_[basic_block]);
#endif
          prev_out.reset();
        }
#ifdef DEBUG
        errs() << "**********************************************************\n";
#endif
      }while(is_anyone_changed_);
    }


    void DoDataFlowAnalysis(Function *F) {
      if(IsForward()) {
        ForwardAnalysis(F);
      } else {
        errs() << "[WARNING] Not supported now!\n";
      }
    }

#ifdef DEBUG
    void TestCorrectBitVectorSub() {
      BitVector a = BitVector(6, false);
      BitVector b = BitVector(6, false);
      // BitVector res;
      BitVector res = BitVector(5, true);

      a[0] = 1;a[2] = 1;a[3] = 1;a[4] = 1;
      b[0] = 1;b[1] = 1;b[4] = 1;b[5] = 1;

      PrintBitVector(a);
      PrintBitVector(b);
      PrintBitVector(res);

      BitVectorSub(a,b,res);

      PrintBitVector(a);
      PrintBitVector(b);
      PrintBitVector(res);
    }

    void TestCorrectMeetInto() {
      BitVector a = BitVector(6, false);
      BitVector b = BitVector(6, false);
      BitVector res;
      // BitVector res = BitVector(5, true);

      a[0] = 1;a[2] = 1;a[3] = 1;a[4] = 1;b[1] = 1;
      //b[0] = 1;b[1] = 1;b[4] = 1;b[5] = 1;

      PrintBitVector(a);
      PrintBitVector(b);
      PrintBitVector(res);

      MeetInto(a,b,res);

      PrintBitVector(a);
      PrintBitVector(b);
      PrintBitVector(res);
    }

    void TestPredecessor(Function *F) {
      for (Function::iterator bb = F->begin(); bb != F->end(); ++bb) {
          BasicBlock *basic_block = dyn_cast<BasicBlock>(bb);
          // if (bb != F->begin()) { // Not entry, need to consider meet operation.
            errs() << " Current block\n";
            errs() << *bb << "\n";
            errs() << " Its predecessors: \n";
            for (BasicBlock *Pred : predecessors(basic_block)) {
              errs() << *Pred << "\n";
            }

          // } 
          
          
        }
    }

    void TestBitVector() {
      BitVector a = BitVector(6, true);
      PrintBitVector(a);
      a.reset();
      PrintBitVector(a);
    }
#endif
    


    bool runOnFunction(Function &F) override {
      DoInit(F);
      // TestCorrectBitVectorSub();
      // TestCorrectMeetInto();
      // TestPredecessor(&F);
      // TestBitVector();
      DoDataFlowAnalysis(&F);

      return false;
    }

    

    // 函数参数类型改了
    // BitVector MeetOp(const BasicBlock & bb) const override
    // {
    //     // @DONE 此处应该是集合交运算后的结果
    //     BitVector result(_domain.size(), true);
    //     for(const BasicBlock* block : predecessors(&bb))
    //     {
    //         // 所有前驱基础块的最后一条Instruction的OUT集合，就是整个基础块的IN集
    //         const Instruction&  last_inst_in_block = block->back();
    //         BitVector curr_bv = _inst_bv_map.at(&last_inst_in_block);
    //         result &= curr_bv;
    //     }
    //     return result;
    // }


    // // bool MeetOp(BasicBlock bb) {

    // // }

    // bool traverseCFG(const Function & func) {
    //   bool transform = false;

    //   for(const BasicBlock& basicBlock : BBTraversalOrder(func)) {
    //       // OUT[B]和IN[B]的节点集合
    //       BitVector ibv;
    //       BitVector obv;
    //       // 确定IN集合
    //       // 注：basicBlock不存在operator==函数，故只能比较其地址是否相同
    //       if(&basicBlock == &(*BBTraversalOrder(func).begin())) {
    //           // 设置OUT[ENTRY]为空集，就是设置EntryBlock的IN集为空集
    //           // 注意基础块ENTRY和此处函数开头的基础块不是同一个
    //           // ENTRY -> funcEntryBlock -> ... -> funcExitBlock -> EXIT
    //           ibv = BC();  // BC 边界条件
    //       }
    //       else {
    //           ibv = MeetOp(basicBlock);
    //       }
    //       // 利用fB=fsn⋅…⋅fs2⋅fs1公式，遍历Instruction，计算出当前基础块的OUT集合
    //       for(const Instruction& inst : InstTraversalOrder(basicBlock)) {
    //           // 对_inst_bv_map的修改在TransferFunc内
    //           // 传入旧的obv，判断是否修改out集。
    //           obv = _inst_bv_map[&inst];
    //           // TransferFunc的第三个参数感觉有点多余
    //           transform |= TransferFunc(inst, ibv, obv);
    //           // 计算出的inst_out集合，是下一次transfer的in集合，所以需要赋值
    //           ibv = obv;
    //       }
    //   }
    //   return transform;
    // }

  };
}

char hytProgramAnalysis::ID = 0;
static RegisterPass<hytProgramAnalysis> X("hytDFA", "Hyt Test Program Analysis Pass");

// namespace {
//   // Hello2 - The second implementation with getAnalysisUsage implemented.
//   struct Hello2 : public FunctionPass {
//     static char ID; // Pass identification, replacement for typeid
//     Hello2() : FunctionPass(ID) {}

//     bool runOnFunction(Function &F) override {
//       ++HelloCounter;
//       errs() << "Hello: ";
//       errs().write_escaped(F.getName()) << '\n';
//       return false;
//     }

//     // We don't modify the program, so we preserve all analyses.
//     void getAnalysisUsage(AnalysisUsage &AU) const override {
//       AU.setPreservesAll();
//     }
//   };
// }

// char Hello2::ID = 0;
// static RegisterPass<Hello2>
// Y("hyt2", "Hello World hyt version Pass (with getAnalysisUsage implemented)");
