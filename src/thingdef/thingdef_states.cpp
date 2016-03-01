/*
** thingdef_states.cpp
**
** Actor definitions - the state parser
**
**---------------------------------------------------------------------------
** Copyright 2002-2007 Christoph Oelckers
** Copyright 2004-2007 Randy Heit
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
** 4. When not used as part of ZDoom or a ZDoom derivative, this code will be
**    covered by the terms of the GNU General Public License as published by
**    the Free Software Foundation; either version 2 of the License, or (at
**    your option) any later version.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

#include "actor.h"
#include "info.h"
#include "sc_man.h"
#include "tarray.h"
#include "templates.h"
#include "cmdlib.h"
#include "p_lnspec.h"
#include "a_action.h"
#include "p_local.h"
#include "v_palette.h"
#include "doomerrors.h"
#include "thingdef.h"
#include "a_sharedglobal.h"
#include "s_sound.h"
#include "i_system.h"
#include "colormatcher.h"
#include "thingdef_exp.h"
#include "version.h"
#include "templates.h"

TDeletingArray<FStateTempCall *> StateTempCalls;

//==========================================================================
//***
// DoActionSpecials
// handles action specials as code pointers
//
//==========================================================================
FxVMFunctionCall *DoActionSpecials(FScanner &sc, FState & state, Baggage &bag)
{
	int i;
	int min_args, max_args;
	FString specname = sc.String;

	int special = P_FindLineSpecial(sc.String, &min_args, &max_args);

	if (special > 0 && min_args >= 0)
	{
		FArgumentList *args = new FArgumentList;
		args->Push(new FxParameter(new FxConstant(special, sc)));
		i = 0;

		// Make this consistent with all other parameter parsing
		if (sc.CheckToken('('))
		{
			while (i < 5)
			{
				args->Push(new FxParameter(new FxIntCast(ParseExpression(sc, bag.Info))));
				i++;
				if (!sc.CheckToken (',')) break;
			}
			sc.MustGetToken (')');
		}

		if (i < min_args)
		{
			sc.ScriptError ("Too few arguments to %s", specname.GetChars());
		}
		if (i > max_args)
		{
			sc.ScriptError ("Too many arguments to %s", specname.GetChars());
		}
		return new FxVMFunctionCall(FindGlobalActionFunction("A_CallSpecial"), args, sc);
	}
	return NULL;
}

//==========================================================================
//***
// Reads a state label that may contain '.'s.
// processes a state block
//
//==========================================================================
static FString ParseStateString(FScanner &sc)
{
	FString statestring;

	sc.MustGetString();
	statestring = sc.String;
	if (sc.CheckString("::"))
	{
		sc.MustGetString ();
		statestring << "::" << sc.String;
	}
	while (sc.CheckString ("."))
	{
		sc.MustGetString ();
		statestring << "." << sc.String;
	}
	return statestring;
}

//==========================================================================
//***
// ParseStates
// parses a state block
//
//==========================================================================
void ParseStates(FScanner &sc, PClassActor * actor, AActor * defaults, Baggage &bag)
{
	FString statestring;
	FState state;
	char lastsprite[5] = "";
	FStateTempCall *tcall = NULL;
	FArgumentList *args = NULL;

	sc.MustGetStringName ("{");
	sc.SetEscape(false);	// disable escape sequences in the state parser
	while (!sc.CheckString ("}") && !sc.End)
	{
		memset(&state,0,sizeof(state));
		statestring = ParseStateString(sc);
		if (!statestring.CompareNoCase("GOTO"))
		{
do_goto:	
			statestring = ParseStateString(sc);
			if (sc.CheckString ("+"))
			{
				sc.MustGetNumber ();
				statestring += '+';
				statestring += sc.String;
			}
			if (!bag.statedef.SetGotoLabel(statestring))
			{
				sc.ScriptError("GOTO before first state");
			}
		}
		else if (!statestring.CompareNoCase("STOP"))
		{
do_stop:
			if (!bag.statedef.SetStop())
			{
				sc.ScriptError("STOP before first state");
				continue;
			}
		}
		else if (!statestring.CompareNoCase("WAIT") || !statestring.CompareNoCase("FAIL"))
		{
			if (!bag.statedef.SetWait())
			{
				sc.ScriptError("%s before first state", sc.String);
				continue;
			}
		}
		else if (!statestring.CompareNoCase("LOOP"))
		{
			if (!bag.statedef.SetLoop())
			{
				sc.ScriptError("LOOP before first state");
				continue;
			}
		}
		else
		{
			sc.MustGetString();
			if (sc.Compare (":"))
			{
				do
				{
					bag.statedef.AddStateLabel(statestring);
					statestring = ParseStateString(sc);
					if (!statestring.CompareNoCase("GOTO"))
					{
						goto do_goto;
					}
					else if (!statestring.CompareNoCase("STOP"))
					{
						goto do_stop;
					}
					sc.MustGetString ();
				} while (sc.Compare (":"));
//				continue;
			}

			sc.UnGet ();

			if (statestring.Len() != 4)
			{
				sc.ScriptError ("Sprite names must be exactly 4 characters\n");
			}

			state.sprite = GetSpriteIndex(statestring);
			state.Misc1 = state.Misc2 = 0;
			sc.MustGetString();
			statestring = sc.String;

			if (tcall == NULL)
			{
				tcall = new FStateTempCall;
			}
			if (sc.CheckString("RANDOM"))
			{
				int min, max;

				sc.MustGetStringName("(");
				sc.MustGetNumber();
				min = clamp<int>(sc.Number, -1, SHRT_MAX);
				sc.MustGetStringName(",");
				sc.MustGetNumber();
				max = clamp<int>(sc.Number, -1, SHRT_MAX);
				sc.MustGetStringName(")");
				if (min > max)
				{
					swapvalues(min, max);
				}
				state.Tics = min;
				state.TicRange = max - min;
			}
			else
			{
				sc.MustGetNumber();
				state.Tics = clamp<int>(sc.Number, -1, SHRT_MAX);
				state.TicRange = 0;
			}

			while (sc.GetString() && (!sc.Crossed || sc.Compare("{")))
			{
				if (sc.Compare("BRIGHT")) 
				{
					state.Fullbright = true;
					continue;
				}
				if (sc.Compare("FAST")) 
				{
					state.Fast = true;
					continue;
				}
				if (sc.Compare("SLOW")) 
				{
					state.Slow = true;
					continue;
				}
				if (sc.Compare("NODELAY"))
				{
					if (bag.statedef.GetStateLabelIndex(NAME_Spawn) == bag.statedef.GetStateCount())
					{
						state.NoDelay = true;
					}
					else
					{
						sc.ScriptMessage("NODELAY may only be used immediately after Spawn:");
					}
					continue;
				}
				if (sc.Compare("OFFSET"))
				{
					// specify a weapon offset
					sc.MustGetStringName("(");
					sc.MustGetNumber();
					state.Misc1 = sc.Number;
					sc.MustGetStringName (",");
					sc.MustGetNumber();
					state.Misc2 = sc.Number;
					sc.MustGetStringName(")");
					continue;
				}
				if (sc.Compare("LIGHT"))
				{
					sc.MustGetStringName("(");
					do
					{
						sc.MustGetString();
						#ifdef DYNLIGHT
							AddStateLight(&state, sc.String);
						#endif
					}
					while (sc.CheckString(","));
					sc.MustGetStringName(")");
					continue;
				}
				if (sc.Compare("CANRAISE"))
				{
					state.CanRaise = true;
					continue;
				}

				bool hasfinalret;
				tcall->Code = ParseActions(sc, state, statestring, bag, tcall->Proto, hasfinalret);
				if (!hasfinalret)
				{
					AddImplicitReturn(static_cast<FxSequence*>(tcall->Code), tcall->Proto, sc);
				}
				goto endofstate;
			}
			sc.UnGet();
endofstate:
			int count = bag.statedef.AddStates(&state, statestring);
			if (count < 0)
			{
				sc.ScriptError ("Invalid frame character string '%s'", statestring.GetChars());
				count = -count;
			}
			if (tcall->Code != NULL)
			{
				tcall->ActorClass = actor;
				tcall->FirstState = bag.statedef.GetStateCount() - count;
				tcall->NumStates = count;
				StateTempCalls.Push(tcall);
				tcall = NULL;
			}
		}
	}
	if (tcall != NULL)
	{
		delete tcall;
	}
	if (args != NULL)
	{
		delete args;
	}
	sc.SetEscape(true);	// re-enable escape sequences
}

//==========================================================================
//
// AddImplicitReturn
//
// Adds an implied return; statement to the end of a code sequence.
//
//==========================================================================

void AddImplicitReturn(FxSequence *code, const PPrototype *proto, FScanner &sc)
{
	if (code == NULL)
	{
		return;
	}
	if (proto == NULL || proto->ReturnTypes.Size() == 0)
	{ // Returns nothing. Good. We can safely add an implied return.
		code->Add(new FxReturnStatement(NULL, sc));
	}
	else
	{ // Something was returned earlier in the sequence. Make it an error
	  // instead of adding an implicit one.
		sc.ScriptError("Not all paths return a value");
	}
}

//==========================================================================
//
// ReturnCheck
//
// If proto1 is NULL, returns proto2. If proto2 is NULL, returns proto1.
// If neither is null, checks if both prototypes define the same return
// types. If not, an error is flagged.
//
//==========================================================================

static PPrototype *ReturnCheck(PPrototype *proto1, PPrototype *proto2, FScanner &sc)
{
	if (proto1 == NULL)
	{
		return proto2;
	}
	if (proto2 == NULL)
	{
		return proto1;
	}
	// A prototype that defines fewer return types can be compatible with
	// one that defines more if the shorter one matches the initial types
	// for the longer one.
	if (proto2->ReturnTypes.Size() < proto1->ReturnTypes.Size())
	{ // Make proto1 the shorter one to avoid code duplication below.
		swapvalues(proto1, proto2);
	}
	// If one prototype returns nothing, they both must.
	if (proto1->ReturnTypes.Size() == 0)
	{
		if (proto2->ReturnTypes.Size() == 0)
		{
			return proto1;
		}
		proto1 = NULL;
	}
	else
	{
		for (unsigned i = 0; i < proto1->ReturnTypes.Size(); ++i)
		{
			if (proto1->ReturnTypes[i] != proto2->ReturnTypes[i])
			{ // Incompatible
				proto1 = NULL;
				break;
			}
		}
	}
	if (proto1 == NULL)
	{
		sc.ScriptError("Return types are incompatible");
	}
	return proto1;
}

//==========================================================================
//
// ParseActions
//
// If this action block contains any return statements, the prototype for
// one of them will be returned. This is used for deducing the return type
// of anonymous functions. All called functions passed to return must have
// matching return types.
//
//==========================================================================

static FxExpression *ParseIf(FScanner &sc, FState state, FString statestring, Baggage &bag,
	PPrototype *&retproto, bool &lastwasret)
{
	FxExpression *add, *cond;
	FxExpression *true_part, *false_part = NULL;
	PPrototype *true_proto, *false_proto = NULL;
	bool true_ret, false_ret = false;
	sc.MustGetStringName("(");
	cond = ParseExpression(sc, bag.Info);
	sc.MustGetStringName(")");
	sc.MustGetStringName("{");	// braces are mandatory
	true_part = ParseActions(sc, state, statestring, bag, true_proto, true_ret);
	sc.MustGetString();
	if (sc.Compare("else"))
	{
		if (sc.CheckString("if"))
		{
			false_part = ParseIf(sc, state, statestring, bag, false_proto, false_ret);
		}
		else
		{
			sc.MustGetStringName("{");	// braces are still mandatory
			false_part = ParseActions(sc, state, statestring, bag, false_proto, false_ret);
			sc.MustGetString();
		}
	}
	add = new FxIfStatement(cond, true_part, false_part, sc);
	retproto = ReturnCheck(retproto, true_proto, sc);
	retproto = ReturnCheck(retproto, false_proto, sc);
	// If one side does not end with a return, we don't consider the if statement
	// to end with a return. If the else case is missing, it can never be considered
	// as ending with a return.
	if (true_ret && false_ret)
	{
		lastwasret = true;
	}
	return add;
}

FxExpression *ParseActions(FScanner &sc, FState state, FString statestring, Baggage &bag,
						   PPrototype *&retproto, bool &endswithret)
{
	// If it's not a '{', then it should be a single action.
	// Otherwise, it's a sequence of actions.
	if (!sc.Compare("{"))
	{
		FxVMFunctionCall *call = ParseAction(sc, state, statestring, bag);
		retproto = call->GetVMFunction()->Proto;
		endswithret = true;
		return new FxReturnStatement(call, sc);
	}

	const FScriptPosition pos(sc);

	FxSequence *seq = NULL;
	PPrototype *proto = NULL;
	bool lastwasret = false;

	sc.MustGetString();
	while (!sc.Compare("}"))
	{
		FxExpression *add;
		lastwasret = false;
		if (sc.Compare("if"))
		{ // Hangle an if statement
			add = ParseIf(sc, state, statestring, bag, proto, lastwasret);
		}
		else if (sc.Compare("return"))
		{ // Handle a return statement
			lastwasret = true;
			FxVMFunctionCall *retexp = NULL;
			PPrototype *retproto;
			sc.MustGetString();
			if (!sc.Compare(";"))
			{
				retexp = ParseAction(sc, state, statestring, bag);
				sc.MustGetStringName(";");
				retproto = retexp->GetVMFunction()->Proto;
			}
			else
			{ // Returning nothing; we still need a prototype for that.
				TArray<PType *> notypes(0);
				retproto = NewPrototype(notypes, notypes);
			}
			proto = ReturnCheck(proto, retproto, sc);
			sc.MustGetString();
			add = new FxReturnStatement(retexp, sc);
		}
		else
		{ // Handle a regular action function call
			add = ParseAction(sc, state, statestring, bag);
			sc.MustGetStringName(";");
			sc.MustGetString();
		}
		// Only return a sequence if it has actual content.
		if (add != NULL)
		{
			if (seq == NULL)
			{
				seq = new FxSequence(pos);
			}
			seq->Add(add);
		}
	}
	endswithret = lastwasret;
	retproto = proto;
	return seq;
}

//==========================================================================
//
// ParseAction
//
//==========================================================================

FxVMFunctionCall *ParseAction(FScanner &sc, FState state, FString statestring, Baggage &bag)
{
	FxVMFunctionCall *call;

	// Make the action name lowercase
	strlwr (sc.String);

	call = DoActionSpecials(sc, state, bag);
	if (call != NULL)
	{
		return call;
	}

	FName symname = FName(sc.String, true);
	symname = CheckCastKludges(symname);
	PFunction *afd = dyn_cast<PFunction>(bag.Info->Symbols.FindSymbol(symname, true));
	if (afd != NULL)
	{
		FArgumentList *args = new FArgumentList;
		ParseFunctionParameters(sc, bag.Info, *args, afd, statestring, &bag.statedef);
		call = new FxVMFunctionCall(afd, args->Size() > 0 ? args : NULL, sc);
		if (args->Size() == 0)
		{
			delete args;
		}
		return call;
	}
	sc.ScriptError("Invalid parameter '%s'\n", sc.String);
	return NULL;
}

//==========================================================================
//
// ParseFunctionParameters
//
// Parses the parameters for a VM function. Called by both ParseStates
// (which will set statestring and statedef) and by ParseExpression0 (which
// will not set them). The first token returned by the scanner when entering
// this function should be '('.
//
//==========================================================================

void ParseFunctionParameters(FScanner &sc, PClassActor *cls, TArray<FxExpression *> &out_params,
	PFunction *afd, FString statestring, FStateDefinitions *statedef)
{
	const TArray<PType *> &params = afd->Variants[0].Implementation->Proto->ArgumentTypes;
	const TArray<DWORD> &paramflags = afd->Variants[0].ArgFlags;
	int numparams = (int)params.Size();
	int pnum = 0;
	bool zeroparm;

	if (afd->Flags & VARF_Method)
	{
		numparams--;
		pnum++;
	}
	if (afd->Flags & VARF_Action)
	{
		numparams -= 2;
		pnum += 2;
	}
	assert(numparams >= 0);
	zeroparm = numparams == 0;
	if (numparams > 0 && !(paramflags[pnum] & VARF_Optional))
	{
		sc.MustGetStringName("(");
	}
	else
	{
		if (!sc.CheckString("(")) 
		{
			return;
		}
	}
	while (numparams > 0)
	{
		FxExpression *x;
		if (statedef != NULL && params[pnum] == TypeState && sc.CheckNumber())
		{
			// Special case: State label as an offset
			if (sc.Number > 0 && statestring.Len() > 1)
			{
				sc.ScriptError("You cannot use state jumps commands with a jump offset on multistate definitions\n");
			}

			int v = sc.Number;
			if (v < 0)
			{
				sc.ScriptError("Negative jump offsets are not allowed");
			}

			if (v > 0)
			{
				x = new FxStateByIndex(statedef->GetStateCount() + v, sc);
			}
			else
			{
				x = new FxConstant((FState*)NULL, sc);
			}
		}
		else
		{
			// Use the generic parameter parser for everything else
			x = ParseParameter(sc, cls, params[pnum], false);
		}
		out_params.Push(new FxParameter(x));
		pnum++;
		numparams--;
		if (numparams > 0)
		{
			if (params[pnum] == NULL)
			{ // varargs function
				if (sc.CheckString(")"))
				{
					return;
				}
				pnum--;
				numparams++;
			}
			else if ((paramflags[pnum] & VARF_Optional) && sc.CheckString(")"))
			{
				return;
			}
			sc.MustGetStringName (",");
		}
	}
	if (zeroparm)
	{
		if (!sc.CheckString(")"))
		{
			sc.ScriptError("You cannot pass parameters to '%s'\n", afd->SymbolName.GetChars());
		}
	}
	else
	{
		sc.MustGetStringName(")");
	}
}

//==========================================================================
//
// CheckCastKludges
//
//==========================================================================

FName CheckCastKludges(FName in)
{
	switch (in)
	{
	case NAME_Int:
		return NAME___decorate_internal_int__;
	case NAME_Bool:
		return NAME___decorate_internal_bool__;
	case NAME_State:
		return NAME___decorate_internal_state__;
	default:
		return in;
	}
}
