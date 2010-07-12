/*
 * Copyright (c) 1996-2009 Barton P. Miller
 * 
 * We provide the Paradyn Parallel Performance Tools (below
 * described as "Paradyn") on an AS IS basis, and do not warrant its
 * validity or performance.  We reserve the right to update, modify,
 * or discontinue this software at any time.  We shall have no
 * obligation to supply such updates or modifications or any other
 * form of support to you.
 * 
 * By your use of Paradyn, you understand and agree that we (or any
 * other person or entity with proprietary rights in Paradyn) are
 * under no obligation to provide either maintenance services,
 * update services, notices of latent defects, or correction of
 * defects for Paradyn.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#if !defined (_R_E_COPY_INSN_H_)
#define _R_E_COPY_INSN_H_

#include "r_e_Base.h"

namespace Dyninst {
namespace Relocation {

class CopyInsn : public Element {
 public:
  typedef dyn_detail::boost::shared_ptr<CopyInsn> Ptr;

  virtual bool generate(Block &, 
			GenStack &);

  static Ptr create(InstructionAPI::Instruction::Ptr insn,
		    Address addr);

  virtual ~CopyInsn() {};

  virtual std::string format() const;

  virtual InstructionAPI::Instruction::Ptr insn() const { return insn_; }
  virtual Address addr() const { return addr_; }
  virtual unsigned size() const { return insn_->size(); }

 private:
  CopyInsn(InstructionAPI::Instruction::Ptr insn,
	    Address addr) : 
  insn_(insn), addr_(addr) {};

  // Pointer to the instruction we represent
  InstructionAPI::Instruction::Ptr insn_;

  // Original address of this instruction
  Address addr_;
};

};
};

#endif