/**************************************************************************/
/*  gdscript_disassembler.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#ifdef DEBUG_ENABLED

#include "gdscript.h"
#include "gdscript_function.h"

#include "core/object/method_bind.h"
#include "core/string/string_builder.h"
#include "core/templates/hash_set.h"

static String _get_variant_string(const Variant &p_variant) {
	String txt;
	if (p_variant.get_type() == Variant::STRING) {
		String s = String(p_variant).c_escape();
		if (s.length() > 48) {
			s = s.left(48) + "...";
		}
		txt = "\"" + s + "\"";
	} else if (p_variant.get_type() == Variant::STRING_NAME) {
		txt = "&\"" + String(p_variant) + "\"";
	} else if (p_variant.get_type() == Variant::NODE_PATH) {
		txt = "^\"" + String(p_variant) + "\"";
	} else if (p_variant.get_type() == Variant::OBJECT) {
		Object *obj = p_variant;
		if (!obj) {
			txt = "null";
		} else {
			GDScriptNativeClass *cls = Object::cast_to<GDScriptNativeClass>(obj);
			if (cls) {
				txt = "class(" + cls->get_name() + ")";
			} else {
				Script *script = Object::cast_to<Script>(obj);
				if (script) {
					txt = "script(" + GDScript::debug_get_script_name(script) + ")";
				} else {
					txt = "object(" + obj->get_class();
					if (obj->get_script_instance()) {
						txt += ", " + GDScript::debug_get_script_name(obj->get_script_instance()->get_script());
					}
					txt += ")";
				}
			}
		}
	} else {
		txt = p_variant;
	}
	return txt;
}

static String _disassemble_address(const GDScript *p_script, const GDScriptFunction &p_function, int p_address) {
	int addr = p_address & GDScriptFunction::ADDR_MASK;

	switch (p_address >> GDScriptFunction::ADDR_BITS) {
		case GDScriptFunction::ADDR_TYPE_STACK: {
			switch (addr) {
				case GDScriptFunction::ADDR_STACK_SELF:
					return "self";
				case GDScriptFunction::ADDR_STACK_CLASS:
					return "class";
				case GDScriptFunction::ADDR_STACK_NIL:
					return "nil";
				default:
					return "r" + itos(addr);
			}
		} break;
		case GDScriptFunction::ADDR_TYPE_CONSTANT: {
			return _get_variant_string(p_function.get_constant(addr));
		} break;
		case GDScriptFunction::ADDR_TYPE_MEMBER: {
			return "self." + String(p_script->debug_get_member_by_index(addr));
		} break;
	}

	return "<err>";
}

String GDScriptFunction::get_disassembly(const Vector<String> &p_code_lines) const {
#define DADDR(m_ip) (_disassemble_address(_script, *this, _code_ptr[ip + m_ip]))

	StringBuilder out;

	String flags;
	if (_static) {
		flags += ", static";
	}
	if (is_vararg()) {
		flags += ", vararg";
	}
	if (_is_native_impl_method) {
		flags += ", native-impl";
	}
	out += vformat("; == func %s  (line %d, %d args, %d stack slots, %d words%s) == \n", String(name), _initial_line, get_argument_count(), _stack_size, _code_size, flags);

	HashSet<int> targets;
	Vector<int> row_ips;
	Vector<String> row_texts;
	Vector<bool> row_markers;

	if (_default_arg_count > 0) {
		out += "; default argument entry points:";
		for (int i = 0; i < _default_arg_count; i++) {
			out += " ";
			out += itos(_default_arg_ptr[i]);
			targets.insert(_default_arg_ptr[i]);
		}
		out += "\n";
	}

	auto target = [&](int p_target) -> String {
		targets.insert(p_target);
		return "-> " + itos(p_target);
	};

	for (int ip = 0; ip < _code_size;) {
		String op;
		StringBuilder text;
		int incr = 0;
		bool is_line_marker = false;
		const int instr_ip = ip;

		// This makes the compiler complain if some opcode is unchecked in the switch.
		Opcode opcode = Opcode(_code_ptr[ip]);

		switch (opcode) {
			case OPCODE_OPERATOR: {
				constexpr int _pointer_size = sizeof(Variant::ValidatedOperatorEvaluator) / sizeof(*_code_ptr);
				int operation = _code_ptr[ip + 4];

				op = "operator";

				text += DADDR(3);
				text += " = ";
				text += DADDR(1);
				text += " ";
				text += Variant::get_operator_name(Variant::Operator(operation));
				text += " ";
				text += DADDR(2);

				incr += 7 + _pointer_size;
			} break;
			case OPCODE_OPERATOR_VALIDATED: {
				op = "validated operator";

				text += DADDR(3);
				text += " = ";
				text += DADDR(1);
				text += " ";
				text += operator_names[_code_ptr[ip + 4]];
				text += " ";
				text += DADDR(2);

				incr += 5;
			} break;
			case OPCODE_TYPE_TEST_BUILTIN: {
				op = "type test";
				text += DADDR(1);
				text += " = ";
				text += DADDR(2);
				text += " is ";
				text += Variant::get_type_name(Variant::Type(_code_ptr[ip + 3]));

				incr += 4;
			} break;
			case OPCODE_TYPE_TEST_ARRAY: {
				op = "type test";
				text += DADDR(1);
				text += " = ";
				text += DADDR(2);
				text += " is Array[";

				Ref<Script> script_type = get_constant(_code_ptr[ip + 3] & ADDR_MASK);
				Variant::Type builtin_type = (Variant::Type)_code_ptr[ip + 4];
				StringName native_type = get_global_name(_code_ptr[ip + 5]);

				if (script_type.is_valid() && script_type->is_script_valid()) {
					text += "script(";
					text += GDScript::debug_get_script_name(script_type);
					text += ")";
				} else if (native_type != StringName()) {
					text += native_type;
				} else {
					text += Variant::get_type_name(builtin_type);
				}

				text += "]";

				incr += 6;
			} break;
			case OPCODE_TYPE_TEST_DICTIONARY: {
				op = "type test";
				text += DADDR(1);
				text += " = ";
				text += DADDR(2);
				text += " is Dictionary[";

				Ref<Script> key_script_type = get_constant(_code_ptr[ip + 3] & ADDR_MASK);
				Variant::Type key_builtin_type = (Variant::Type)_code_ptr[ip + 5];
				StringName key_native_type = get_global_name(_code_ptr[ip + 6]);

				if (key_script_type.is_valid() && key_script_type->is_script_valid()) {
					text += "script(";
					text += GDScript::debug_get_script_name(key_script_type);
					text += ")";
				} else if (key_native_type != StringName()) {
					text += key_native_type;
				} else {
					text += Variant::get_type_name(key_builtin_type);
				}

				text += ", ";

				Ref<Script> value_script_type = get_constant(_code_ptr[ip + 4] & ADDR_MASK);
				Variant::Type value_builtin_type = (Variant::Type)_code_ptr[ip + 7];
				StringName value_native_type = get_global_name(_code_ptr[ip + 8]);

				if (value_script_type.is_valid() && value_script_type->is_script_valid()) {
					text += "script(";
					text += GDScript::debug_get_script_name(value_script_type);
					text += ")";
				} else if (value_native_type != StringName()) {
					text += value_native_type;
				} else {
					text += Variant::get_type_name(value_builtin_type);
				}

				text += "]";

				incr += 9;
			} break;
			case OPCODE_TYPE_TEST_NATIVE: {
				op = "type test";
				text += DADDR(1);
				text += " = ";
				text += DADDR(2);
				text += " is ";
				text += get_global_name(_code_ptr[ip + 3]);

				incr += 4;
			} break;
			case OPCODE_TYPE_TEST_SCRIPT: {
				op = "type test";
				text += DADDR(1);
				text += " = ";
				text += DADDR(2);
				text += " is ";
				text += DADDR(3);

				incr += 4;
			} break;
			case OPCODE_TYPE_TEST_TRAIT: {
				op = "trait test";
				text += DADDR(1);
				text += " = ";
				text += DADDR(2);
				text += " is ";
				text += get_global_name(_code_ptr[ip + 3]);

				incr += 4;
			} break;
			case OPCODE_SET_KEYED: {
				op = "set keyed";
				text += DADDR(1);
				text += "[";
				text += DADDR(2);
				text += "] = ";
				text += DADDR(3);

				incr += 4;
			} break;
			case OPCODE_SET_KEYED_VALIDATED: {
				op = "set keyed validated";
				text += DADDR(1);
				text += "[";
				text += DADDR(2);
				text += "] = ";
				text += DADDR(3);

				incr += 5;
			} break;
			case OPCODE_SET_INDEXED_VALIDATED: {
				op = "set indexed validated";
				text += DADDR(1);
				text += "[";
				text += DADDR(2);
				text += "] = ";
				text += DADDR(3);

				incr += 5;
			} break;
			case OPCODE_GET_KEYED: {
				op = "get keyed";
				text += DADDR(3);
				text += " = ";
				text += DADDR(1);
				text += "[";
				text += DADDR(2);
				text += "]";

				incr += 4;
			} break;
			case OPCODE_GET_KEYED_VALIDATED: {
				op = "get keyed validated";
				text += DADDR(3);
				text += " = ";
				text += DADDR(1);
				text += "[";
				text += DADDR(2);
				text += "]";

				incr += 5;
			} break;
			case OPCODE_GET_INDEXED_VALIDATED: {
				op = "get indexed validated";
				text += DADDR(3);
				text += " = ";
				text += DADDR(1);
				text += "[";
				text += DADDR(2);
				text += "]";

				incr += 5;
			} break;
			case OPCODE_SET_NAMED: {
				op = "set_named";
				text += DADDR(1);
				text += "[\"";
				text += _global_names_ptr[_code_ptr[ip + 3]];
				text += "\"] = ";
				text += DADDR(2);

				incr += 4;
			} break;
			case OPCODE_SET_NAMED_VALIDATED: {
				op = "set_named validated";
				text += DADDR(1);
				text += "[\"";
				text += setter_names[_code_ptr[ip + 3]];
				text += "\"] = ";
				text += DADDR(2);

				incr += 4;
			} break;
			case OPCODE_GET_NAMED: {
				op = "get_named";
				text += DADDR(2);
				text += " = ";
				text += DADDR(1);
				text += "[\"";
				text += _global_names_ptr[_code_ptr[ip + 3]];
				text += "\"]";

				incr += 4;
			} break;
			case OPCODE_GET_NAMED_VALIDATED: {
				op = "get_named validated";
				text += DADDR(2);
				text += " = ";
				text += DADDR(1);
				text += "[\"";
				text += getter_names[_code_ptr[ip + 3]];
				text += "\"]";

				incr += 4;
			} break;
			case OPCODE_SET_MEMBER: {
				op = "set_member";
				text += "[\"";
				text += _global_names_ptr[_code_ptr[ip + 2]];
				text += "\"] = ";
				text += DADDR(1);

				incr += 3;
			} break;
			case OPCODE_GET_MEMBER: {
				op = "get_member";
				text += DADDR(1);
				text += " = ";
				text += "[\"";
				text += _global_names_ptr[_code_ptr[ip + 2]];
				text += "\"]";

				incr += 3;
			} break;
			case OPCODE_SET_MEMBER_VALIDATED: {
				const MethodBind* setter = _methods_ptr[_code_ptr[ip + 2]];
				int baked_index = _code_ptr[ip + 3];

				op = "set_member validated";
				text += "[\"";
				text += setter->get_name();
				text += "\"";
				if (baked_index >= 0) {
					text += ", index ";
					text += itos(baked_index);
				}
				text += "] = ";
				text += DADDR(1);

				incr += 4;
			} break;
			case OPCODE_GET_MEMBER_VALIDATED: {
				const MethodBind* getter = _methods_ptr[_code_ptr[ip + 2]];
				int baked_index = _code_ptr[ip + 3];

				op = "get_member validated";
				text += DADDR(1);
				text += " = [\"";
				text += getter->get_name();
				text += "\"";
				if (baked_index >= 0) {
					text += ", index ";
					text += itos(baked_index);
				}
				text += "]";

				incr += 4;
			} break;
			case OPCODE_SET_NAMED_MEMBER_VALIDATED: {
				const MethodBind* setter = _methods_ptr[_code_ptr[ip + 3]];
				int baked_index = _code_ptr[ip + 4];

				op = "set_named_member validated";
				text += DADDR(1);
				text += "[\"";
				text += setter->get_name();
				text += "\"";
				if (baked_index >= 0) {
					text += ", index ";
					text += itos(baked_index);
				}
				text += "] = ";
				text += DADDR(2);

				incr += 5;
			} break;
			case OPCODE_GET_NAMED_MEMBER_VALIDATED: {
				const MethodBind* getter = _methods_ptr[_code_ptr[ip + 3]];
				int baked_index = _code_ptr[ip + 4];

				op = "get_named_member validated";
				text += DADDR(2);
				text += " = ";
				text += DADDR(1);
				text += "[\"";
				text += getter->get_name();
				text += "\"";
				if (baked_index >= 0) {
					text += ", index ";
					text += itos(baked_index);
				}
				text += "]";

				incr += 5;
			} break;
			case OPCODE_GET_NAMED_ENUM_IMPL_CACHED: {
				GDScriptFunction* impl_function = _native_impl_call_functions_ptr[_code_ptr[ip + 3]];

				op = "get_named enum-impl cached";
				text += DADDR(2);
				text += " = ";
				text += DADDR(1);
				text += " bound to ";
				text += impl_function != nullptr ? String(impl_function->get_name()) : String("<null>");

				incr += 4;
			} break;
			case OPCODE_CHECK_TYPED_ARRAY_ARG: {
				Ref<Script> script_type = get_constant(_code_ptr[ip + 2] & ADDR_MASK);
				Variant::Type builtin_type = (Variant::Type)_code_ptr[ip + 4];
				StringName native_type = get_global_name(_code_ptr[ip + 5]);

				String type_name;
				if (script_type.is_valid() && script_type->is_script_valid()) {
					type_name = "script(" + GDScript::debug_get_script_name(script_type) + ")";
				} else if (native_type != StringName()) {
					type_name = native_type;
				} else {
					type_name = Variant::get_type_name(builtin_type);
				}

				op = "check typed array arg";
				text += "(Array[";
				text += type_name;
				text += "]) ";
				text += DADDR(3);
				text += " = ";
				text += DADDR(1);

				incr += 6;
			} break;
			case OPCODE_CHECK_TYPED_DICTIONARY_ARG: {
				Ref<Script> key_script_type = get_constant(_code_ptr[ip + 2] & ADDR_MASK);
				Variant::Type key_builtin_type = (Variant::Type)_code_ptr[ip + 5];
				StringName key_native_type = get_global_name(_code_ptr[ip + 6]);

				String key_type_name;
				if (key_script_type.is_valid() && key_script_type->is_script_valid()) {
					key_type_name = "script(" + GDScript::debug_get_script_name(key_script_type) + ")";
				} else if (key_native_type != StringName()) {
					key_type_name = key_native_type;
				} else {
					key_type_name = Variant::get_type_name(key_builtin_type);
				}

				Ref<Script> value_script_type = get_constant(_code_ptr[ip + 3] & ADDR_MASK);
				Variant::Type value_builtin_type = (Variant::Type)_code_ptr[ip + 7];
				StringName value_native_type = get_global_name(_code_ptr[ip + 8]);

				String value_type_name;
				if (value_script_type.is_valid() && value_script_type->is_script_valid()) {
					value_type_name = "script(" + GDScript::debug_get_script_name(value_script_type) + ")";
				} else if (value_native_type != StringName()) {
					value_type_name = value_native_type;
				} else {
					value_type_name = Variant::get_type_name(value_builtin_type);
				}

				op = "check typed dictionary arg";
				text += "(Dictionary[";
				text += key_type_name;
				text += ", ";
				text += value_type_name;
				text += "]) ";
				text += DADDR(4);
				text += " = ";
				text += DADDR(1);

				incr += 9;
			} break;
			case OPCODE_SET_STATIC_VARIABLE: {
				Ref<GDScript> gdscript;
				if (_code_ptr[ip + 2] == ADDR_CLASS) {
					gdscript = Ref<GDScript>(_script);
				} else {
					gdscript = get_constant(_code_ptr[ip + 2] & ADDR_MASK);
				}

				op = "set_static_variable";
				text += "script(";
				text += GDScript::debug_get_script_name(gdscript);
				text += ")";
				if (gdscript.is_valid()) {
					text += "[\"" + gdscript->debug_get_static_var_by_index(_code_ptr[ip + 3]) + "\"]";
				} else {
					text += "[<index " + itos(_code_ptr[ip + 3]) + ">]";
				}
				text += " = ";
				text += DADDR(1);

				incr += 4;
			} break;
			case OPCODE_GET_STATIC_VARIABLE: {
				Ref<GDScript> gdscript;
				if (_code_ptr[ip + 2] == ADDR_CLASS) {
					gdscript = Ref<GDScript>(_script);
				} else {
					gdscript = get_constant(_code_ptr[ip + 2] & ADDR_MASK);
				}

				op = "get_static_variable";
				text += DADDR(1);
				text += " = script(";
				text += GDScript::debug_get_script_name(gdscript);
				text += ")";
				if (gdscript.is_valid()) {
					text += "[\"" + gdscript->debug_get_static_var_by_index(_code_ptr[ip + 3]) + "\"]";
				} else {
					text += "[<index " + itos(_code_ptr[ip + 3]) + ">]";
				}

				incr += 4;
			} break;
			case OPCODE_ASSIGN: {
				op = "assign";
				text += DADDR(1);
				text += " = ";
				text += DADDR(2);

				incr += 3;
			} break;
			case OPCODE_ASSIGN_NULL: {
				op = "assign";
				text += DADDR(1);
				text += " = null";

				incr += 2;
			} break;
			case OPCODE_ASSIGN_TRUE: {
				op = "assign";
				text += DADDR(1);
				text += " = true";

				incr += 2;
			} break;
			case OPCODE_ASSIGN_FALSE: {
				op = "assign";
				text += DADDR(1);
				text += " = false";

				incr += 2;
			} break;
			case OPCODE_ASSIGN_TYPED_BUILTIN: {
				op = "assign typed builtin";
				text += "(";
				text += Variant::get_type_name((Variant::Type)_code_ptr[ip + 3]);
				text += ") ";
				text += DADDR(1);
				text += " = ";
				text += DADDR(2);

				incr += 4;
			} break;
			case OPCODE_ASSIGN_TYPED_ARRAY: {
				op = "assign typed array";
				text += DADDR(1);
				text += " = ";
				text += DADDR(2);

				incr += 6;
			} break;
			case OPCODE_ASSIGN_TYPED_ARRAY_NESTED: {
				op = "assign typed nested array";
				text += DADDR(1);
				text += " = ";
				text += DADDR(2);

				incr += 7;
			} break;
			case OPCODE_ASSIGN_TYPED_DICTIONARY: {
				op = "assign typed dictionary";
				text += DADDR(1);
				text += " = ";
				text += DADDR(2);

				incr += 9;
			} break;
			case OPCODE_ASSIGN_TYPED_DICTIONARY_NESTED: {
				op = "assign typed nested dictionary";
				text += DADDR(1);
				text += " = ";
				text += DADDR(2);
				incr += 10;
			} break;
			case OPCODE_ASSIGN_TYPED_NATIVE: {
				op = "assign typed native";
				text += "(";
				text += DADDR(3);
				text += ") ";
				text += DADDR(1);
				text += " = ";
				text += DADDR(2);

				incr += 4;
			} break;
			case OPCODE_ASSIGN_TYPED_SCRIPT: {
				Ref<Script> script = get_constant(_code_ptr[ip + 3] & ADDR_MASK);

				op = "assign typed script";
				text += "(";
				text += GDScript::debug_get_script_name(script);
				text += ") ";
				text += DADDR(1);
				text += " = ";
				text += DADDR(2);

				incr += 4;
			} break;
			case OPCODE_CAST_TO_BUILTIN: {
				op = "cast builtin";
				text += DADDR(2);
				text += " = ";
				text += DADDR(1);
				text += " as ";
				text += Variant::get_type_name(Variant::Type(_code_ptr[ip + 3]));

				incr += 4;
			} break;
			case OPCODE_CAST_TO_NATIVE: {
				op = "cast native";
				text += DADDR(2);
				text += " = ";
				text += DADDR(1);
				text += " as ";
				text += DADDR(3);

				incr += 4;
			} break;
			case OPCODE_CAST_TO_SCRIPT: {
				op = "cast";
				text += DADDR(2);
				text += " = ";
				text += DADDR(1);
				text += " as ";
				text += DADDR(3);

				incr += 4;
			} break;
			case OPCODE_CONSTRUCT: {
				int instr_var_args = _code_ptr[++ip];
				Variant::Type t = Variant::Type(_code_ptr[ip + 3 + instr_var_args]);
				int argc = _code_ptr[ip + 1 + instr_var_args];

				op = "construct";
				text += DADDR(1 + argc);
				text += " = ";

				text += Variant::get_type_name(t) + "(";
				for (int i = 0; i < argc; i++) {
					if (i > 0) {
						text += ", ";
					}
					text += DADDR(i + 1);
				}
				text += ")";

				incr = 3 + instr_var_args;
			} break;
			case OPCODE_CONSTRUCT_VALIDATED: {
				int instr_var_args = _code_ptr[++ip];
				int argc = _code_ptr[ip + 1 + instr_var_args];

				op = "construct validated";
				text += DADDR(1 + argc);
				text += " = ";

				text += constructors_names[_code_ptr[ip + 3 + argc]];
				text += "(";
				for (int i = 0; i < argc; i++) {
					if (i > 0) {
						text += ", ";
					}
					text += DADDR(i + 1);
				}
				text += ")";

				incr = 3 + instr_var_args;
			} break;
			case OPCODE_CONSTRUCT_ARRAY: {
				int instr_var_args = _code_ptr[++ip];
				int argc = _code_ptr[ip + 1 + instr_var_args];
				op = "make_array";
				text += DADDR(1 + argc);
				text += " = [";

				for (int i = 0; i < argc; i++) {
					if (i > 0) {
						text += ", ";
					}
					text += DADDR(1 + i);
				}

				text += "]";

				incr += 3 + argc;
			} break;
			case OPCODE_CONSTRUCT_TYPED_ARRAY: {
				int instr_var_args = _code_ptr[++ip];
				int argc = _code_ptr[ip + 1 + instr_var_args];

				Ref<Script> script_type = get_constant(_code_ptr[ip + argc + 2] & ADDR_MASK);
				Variant::Type builtin_type = (Variant::Type)_code_ptr[ip + argc + 4];
				StringName native_type = get_global_name(_code_ptr[ip + argc + 5]);

				String type_name;
				if (script_type.is_valid() && script_type->is_script_valid()) {
					type_name = "script(" + GDScript::debug_get_script_name(script_type) + ")";
				} else if (native_type != StringName()) {
					type_name = native_type;
				} else {
					type_name = Variant::get_type_name(builtin_type);
				}

				op = "make_typed_array";
				text += "(";
				text += type_name;
				text += ") ";

				text += DADDR(1 + argc);
				text += " = [";

				for (int i = 0; i < argc; i++) {
					if (i > 0) {
						text += ", ";
					}
					text += DADDR(1 + i);
				}

				text += "]";

				incr += 6 + argc;
			} break;
			case OPCODE_CONSTRUCT_DICTIONARY: {
				int instr_var_args = _code_ptr[++ip];
				int argc = _code_ptr[ip + 1 + instr_var_args];
				op = "make_dict";
				text += DADDR(1 + argc * 2);
				text += " = {";

				for (int i = 0; i < argc; i++) {
					if (i > 0) {
						text += ", ";
					}
					text += DADDR(1 + i * 2 + 0);
					text += ": ";
					text += DADDR(1 + i * 2 + 1);
				}

				text += "}";

				incr += 3 + argc * 2;
			} break;
			case OPCODE_CONSTRUCT_TYPED_DICTIONARY: {
				int instr_var_args = _code_ptr[++ip];
				int argc = _code_ptr[ip + 1 + instr_var_args];

				Ref<Script> key_script_type = get_constant(_code_ptr[ip + argc * 2 + 2] & ADDR_MASK);
				Variant::Type key_builtin_type = (Variant::Type)_code_ptr[ip + argc * 2 + 5];
				StringName key_native_type = get_global_name(_code_ptr[ip + argc * 2 + 6]);

				String key_type_name;
				if (key_script_type.is_valid() && key_script_type->is_script_valid()) {
					key_type_name = "script(" + GDScript::debug_get_script_name(key_script_type) + ")";
				} else if (key_native_type != StringName()) {
					key_type_name = key_native_type;
				} else {
					key_type_name = Variant::get_type_name(key_builtin_type);
				}

				Ref<Script> value_script_type = get_constant(_code_ptr[ip + argc * 2 + 3] & ADDR_MASK);
				Variant::Type value_builtin_type = (Variant::Type)_code_ptr[ip + argc * 2 + 7];
				StringName value_native_type = get_global_name(_code_ptr[ip + argc * 2 + 8]);

				String value_type_name;
				if (value_script_type.is_valid() && value_script_type->is_script_valid()) {
					value_type_name = "script(" + GDScript::debug_get_script_name(value_script_type) + ")";
				} else if (value_native_type != StringName()) {
					value_type_name = value_native_type;
				} else {
					value_type_name = Variant::get_type_name(value_builtin_type);
				}

				op = "make_typed_dict";
				text += "(";
				text += key_type_name;
				text += ", ";
				text += value_type_name;
				text += ") ";

				text += DADDR(1 + argc * 2);
				text += " = {";

				for (int i = 0; i < argc; i++) {
					if (i > 0) {
						text += ", ";
					}
					text += DADDR(1 + i * 2 + 0);
					text += ": ";
					text += DADDR(1 + i * 2 + 1);
				}

				text += "}";

				incr += 9 + argc * 2;
			} break;
			case OPCODE_CALL:
			case OPCODE_CALL_RETURN:
			case OPCODE_CALL_ASYNC: {
				bool ret = (_code_ptr[ip]) == OPCODE_CALL_RETURN;
				bool async = (_code_ptr[ip]) == OPCODE_CALL_ASYNC;

				int instr_var_args = _code_ptr[++ip];

				op = ret ? "call-ret" : (async ? "call-async" : "call");

				int argc = _code_ptr[ip + 1 + instr_var_args];
				if (ret || async) {
					text += DADDR(2 + argc) + " = ";
				}

				text += DADDR(1 + argc) + ".";
				text += String(_global_names_ptr[_code_ptr[ip + 2 + instr_var_args]]);
				text += "(";

				for (int i = 0; i < argc; i++) {
					if (i > 0) {
						text += ", ";
					}
					text += DADDR(1 + i);
				}
				text += ")";

				incr = 5 + argc;
			} break;
			case OPCODE_CALL_METHOD_BIND:
			case OPCODE_CALL_METHOD_BIND_RET: {
				bool ret = (_code_ptr[ip]) == OPCODE_CALL_METHOD_BIND_RET;
				int instr_var_args = _code_ptr[++ip];

				op = ret ? "call-method_bind-ret" : "call-method_bind";

				const MethodBind *method = _methods_ptr[_code_ptr[ip + 2 + instr_var_args]];

				int argc = _code_ptr[ip + 1 + instr_var_args];
				if (ret) {
					text += DADDR(2 + argc) + " = ";
				}

				text += DADDR(1 + argc) + ".";
				text += method->get_name();
				text += "(";

				for (int i = 0; i < argc; i++) {
					if (i > 0) {
						text += ", ";
					}
					text += DADDR(1 + i);
				}
				text += ")";

				incr = 5 + argc;
			} break;
			case OPCODE_CALL_BUILTIN_STATIC: {
				int instr_var_args = _code_ptr[++ip];
				Variant::Type type = (Variant::Type)_code_ptr[ip + 1 + instr_var_args];
				int argc = _code_ptr[ip + 3 + instr_var_args];

				op = "call built-in method static";
				text += DADDR(1 + argc);
				text += " = ";
				text += Variant::get_type_name(type);
				text += ".";
				text += _global_names_ptr[_code_ptr[ip + 2 + instr_var_args]].string();
				text += "(";

				for (int i = 0; i < argc; i++) {
					if (i > 0) {
						text += ", ";
					}
					text += DADDR(1 + i);
				}
				text += ")";

				incr += 5 + argc;
			} break;
			case OPCODE_CALL_NATIVE_STATIC: {
				int instr_var_args = _code_ptr[++ip];
				const MethodBind *method = _methods_ptr[_code_ptr[ip + 1 + instr_var_args]];
				int argc = _code_ptr[ip + 2 + instr_var_args];

				op = "call native method static";
				text += DADDR(1 + argc);
				text += " = ";
				text += method->get_instance_class();
				text += ".";
				text += method->get_name();
				text += "(";

				for (int i = 0; i < argc; i++) {
					if (i > 0) {
						text += ", ";
					}
					text += DADDR(1 + i);
				}
				text += ")";

				incr += 4 + argc;
			} break;

			case OPCODE_CALL_NATIVE_STATIC_VALIDATED_RETURN: {
				int instr_var_args = _code_ptr[++ip];
				op = "call native static method validated";
				text += "(return) ";
				const MethodBind *method = _methods_ptr[_code_ptr[ip + 2 + instr_var_args]];
				int argc = _code_ptr[ip + 1 + instr_var_args];
				text += DADDR(1 + argc) + " = ";
				text += method->get_instance_class();
				text += ".";
				text += method->get_name();
				text += "(";
				for (int i = 0; i < argc; i++) {
					if (i > 0) {
						text += ", ";
					}
					text += DADDR(1 + i);
				}
				text += ")";
				incr = 5 + argc;
			} break;

			case OPCODE_CALL_NATIVE_STATIC_VALIDATED_NO_RETURN: {
				int instr_var_args = _code_ptr[++ip];

				op = "call native static method validated";
				text += "(no return) ";

				const MethodBind *method = _methods_ptr[_code_ptr[ip + 2 + instr_var_args]];

				int argc = _code_ptr[ip + 1 + instr_var_args];

				text += method->get_instance_class();
				text += ".";
				text += method->get_name();
				text += "(";

				for (int i = 0; i < argc; i++) {
					if (i > 0) {
						text += ", ";
					}
					text += DADDR(1 + i);
				}
				text += ")";

				incr = 4 + argc;
			} break;

			case OPCODE_CALL_METHOD_BIND_VALIDATED_RETURN: {
				int instr_var_args = _code_ptr[++ip];
				op = "call method-bind validated";
				text += "(return) ";
				const MethodBind *method = _methods_ptr[_code_ptr[ip + 2 + instr_var_args]];
				int argc = _code_ptr[ip + 1 + instr_var_args];
				text += DADDR(2 + argc) + " = ";
				text += DADDR(1 + argc) + ".";
				text += method->get_name();
				text += "(";
				for (int i = 0; i < argc; i++) {
					if (i > 0) {
						text += ", ";
					}
					text += DADDR(1 + i);
				}
				text += ")";
				incr = 5 + argc;
			} break;

			case OPCODE_CALL_METHOD_BIND_VALIDATED_NO_RETURN: {
				int instr_var_args = _code_ptr[++ip];

				op = "call method-bind validated";
				text += "(no return) ";

				const MethodBind *method = _methods_ptr[_code_ptr[ip + 2 + instr_var_args]];

				int argc = _code_ptr[ip + 1 + instr_var_args];

				text += DADDR(1 + argc) + ".";
				text += method->get_name();
				text += "(";

				for (int i = 0; i < argc; i++) {
					if (i > 0) {
						text += ", ";
					}
					text += DADDR(1 + i);
				}
				text += ")";

				incr = 5 + argc;
			} break;

			case OPCODE_CALL_NATIVE_IMPL_CACHED:
			case OPCODE_CALL_NATIVE_IMPL_CACHED_RET: {
				bool call_ret = _code_ptr[ip] == OPCODE_CALL_NATIVE_IMPL_CACHED_RET;
				int instr_var_args = _code_ptr[++ip];
				int argc = _code_ptr[ip + 1 + instr_var_args];
				GDScriptFunction* function = _native_impl_call_functions_ptr[_code_ptr[ip + 2 + instr_var_args]];

				op = call_ret ? "call native-impl cached (return)" : "call native-impl cached (no return)";
				if (call_ret) {
					text += DADDR(2 + argc) + " = ";
				}
				text += DADDR(1 + argc) + ".";
				text += function != nullptr ? String(function->get_name()) : String("<null>");
				text += "(";
				for (int i = 0; i < argc; i++) {
					if (i > 0) {
						text += ", ";
					}
					text += DADDR(1 + i);
				}
				text += ")";

				incr = 5 + argc;
			} break;

			case OPCODE_CALL_BUILTIN_TYPE_VALIDATED: {
				int instr_var_args = _code_ptr[++ip];
				int argc = _code_ptr[ip + 1 + instr_var_args];

				op = "call-builtin-method validated";

				text += DADDR(2 + argc) + " = ";

				text += DADDR(1) + ".";
				text += builtin_methods_names[_code_ptr[ip + 4 + argc]];

				text += "(";

				for (int i = 0; i < argc; i++) {
					if (i > 0) {
						text += ", ";
					}
					text += DADDR(2 + i);
				}
				text += ")";

				incr = 5 + argc;
			} break;
			case OPCODE_CALL_UTILITY: {
				int instr_var_args = _code_ptr[++ip];

				op = "call-utility";

				int argc = _code_ptr[ip + 1 + instr_var_args];
				text += DADDR(1 + argc) + " = ";

				text += _global_names_ptr[_code_ptr[ip + 2 + instr_var_args]];
				text += "(";

				for (int i = 0; i < argc; i++) {
					if (i > 0) {
						text += ", ";
					}
					text += DADDR(1 + i);
				}
				text += ")";

				incr = 4 + argc;
			} break;
			case OPCODE_CALL_UTILITY_VALIDATED: {
				int instr_var_args = _code_ptr[++ip];

				op = "call-utility validated";

				int argc = _code_ptr[ip + 1 + instr_var_args];
				text += DADDR(1 + argc) + " = ";

				text += utilities_names[_code_ptr[ip + 3 + argc]];
				text += "(";

				for (int i = 0; i < argc; i++) {
					if (i > 0) {
						text += ", ";
					}
					text += DADDR(1 + i);
				}
				text += ")";

				incr = 4 + argc;
			} break;
			case OPCODE_CALL_GDSCRIPT_UTILITY: {
				int instr_var_args = _code_ptr[++ip];

				op = "call-gdscript-utility";

				int argc = _code_ptr[ip + 1 + instr_var_args];
				text += DADDR(1 + argc) + " = ";

				text += gds_utilities_names[_code_ptr[ip + 3 + argc]];
				text += "(";

				for (int i = 0; i < argc; i++) {
					if (i > 0) {
						text += ", ";
					}
					text += DADDR(1 + i);
				}
				text += ")";

				incr = 4 + argc;
			} break;
			case OPCODE_CALL_SELF_BASE: {
				int instr_var_args = _code_ptr[++ip];

				op = "call-self-base";

				int argc = _code_ptr[ip + 1 + instr_var_args];
				text += DADDR(2 + argc) + " = ";

				text += _global_names_ptr[_code_ptr[ip + 2 + instr_var_args]];
				text += "(";

				for (int i = 0; i < argc; i++) {
					if (i > 0) {
						text += ", ";
					}
					text += DADDR(1 + i);
				}
				text += ")";

				incr = 4 + argc;
			} break;
			case OPCODE_AWAIT: {
				op = "await";
				text += DADDR(1);

				incr = 2;
			} break;
			case OPCODE_AWAIT_RESUME: {
				op = "await resume";
				text += DADDR(1);

				incr = 2;
			} break;
			case OPCODE_CREATE_LAMBDA: {
				int instr_var_args = _code_ptr[++ip];
				int captures_count = _code_ptr[ip + 1 + instr_var_args];
				GDScriptFunction *lambda = _lambdas_ptr[_code_ptr[ip + 2 + instr_var_args]];

				op = "create lambda";
				text += DADDR(1 + captures_count);
				text += " = ";
				text += lambda->name.string();
				text += ", captures (";

				for (int i = 0; i < captures_count; i++) {
					if (i > 0) {
						text += ", ";
					}
					text += DADDR(1 + i);
				}
				text += ")";

				incr = 4 + captures_count;
			} break;
			case OPCODE_CREATE_SELF_LAMBDA: {
				int instr_var_args = _code_ptr[++ip];
				int captures_count = _code_ptr[ip + 1 + instr_var_args];
				GDScriptFunction *lambda = _lambdas_ptr[_code_ptr[ip + 2 + instr_var_args]];

				op = "create self lambda";
				text += DADDR(1 + captures_count);
				text += " = ";
				text += lambda->name.string();
				text += ", captures (";

				for (int i = 0; i < captures_count; i++) {
					if (i > 0) {
						text += ", ";
					}
					text += DADDR(1 + i);
				}
				text += ")";

				incr = 4 + captures_count;
			} break;
			case OPCODE_JUMP: {
				op = "jump";
				text += target(_code_ptr[ip + 1]);

				incr = 2;
			} break;
			case OPCODE_JUMP_IF: {
				op = "jump-if";
				text += DADDR(1);
				text += " ";
				text += target(_code_ptr[ip + 2]);

				incr = 3;
			} break;
			case OPCODE_JUMP_IF_NOT: {
				op = "jump-if-not";
				text += DADDR(1);
				text += " ";
				text += target(_code_ptr[ip + 2]);

				incr = 3;
			} break;
			case OPCODE_JUMP_TO_DEF_ARGUMENT: {
				op = "jump-to-default-argument";

				incr = 1;
			} break;
			case OPCODE_JUMP_IF_SHARED: {
				op = "jump-if-shared";
				text += DADDR(1);
				text += " ";
				text += target(_code_ptr[ip + 2]);

				incr = 3;
			} break;
			case OPCODE_RETURN: {
				op = "return";
				text += DADDR(1);

				incr = 2;
			} break;
			case OPCODE_RETURN_TYPED_BUILTIN: {
				op = "return typed builtin";
				text += "(";
				text += Variant::get_type_name((Variant::Type)_code_ptr[ip + 2]);
				text += ") ";
				text += DADDR(1);

				incr += 3;
			} break;
			case OPCODE_RETURN_TYPED_ARRAY: {
				op = "return typed array";
				text += DADDR(1);

				incr += 5;
			} break;
			case OPCODE_RETURN_TYPED_DICTIONARY: {
				op = "return typed dictionary";
				text += DADDR(1);

				incr += 8;
			} break;
			case OPCODE_RETURN_TYPED_DICTIONARY_NESTED: {
				op = "return typed nested dictionary";
				text += DADDR(1);
				incr += 9;
			} break;
			case OPCODE_RETURN_TYPED_NATIVE: {
				op = "return typed native";
				text += "(";
				text += DADDR(2);
				text += ") ";
				text += DADDR(1);

				incr += 3;
			} break;
			case OPCODE_RETURN_TYPED_SCRIPT: {
				Ref<Script> script = get_constant(_code_ptr[ip + 2] & ADDR_MASK);

				op = "return typed script";
				text += "(";
				text += GDScript::debug_get_script_name(script);
				text += ") ";
				text += DADDR(1);

				incr += 3;
			} break;
			case OPCODE_RESERVED_0:
			case OPCODE_RESERVED_1:
			case OPCODE_RESERVED_2:
			case OPCODE_RESERVED_3:
			case OPCODE_RESERVED_4:
			case OPCODE_RESERVED_5:
			case OPCODE_RESERVED_6:
			case OPCODE_RESERVED_7:
			case OPCODE_RESERVED_8:
			case OPCODE_RESERVED_9:
			case OPCODE_RESERVED_10:
			case OPCODE_RESERVED_11:
			case OPCODE_RESERVED_12:
			case OPCODE_RESERVED_13:
			case OPCODE_RESERVED_14: {
				op = "<reserved opcode>";
				incr = 1;
			} break;

#define DISASSEMBLE_ITERATE(m_type) \
	case OPCODE_ITERATE_##m_type: { \
		op = "for-loop"; \
		text += "(typed "; \
		text += #m_type; \
		text += ") "; \
		text += DADDR(3); \
		text += " in "; \
		text += DADDR(2); \
		text += " counter "; \
		text += DADDR(1); \
		text += " exit "; \
		text += target(_code_ptr[ip + 4]); \
		incr += 5; \
	} break

#define DISASSEMBLE_ITERATE_BEGIN(m_type) \
	case OPCODE_ITERATE_BEGIN_##m_type: { \
		op = "for-init"; \
		text += "(typed "; \
		text += #m_type; \
		text += ") "; \
		text += DADDR(3); \
		text += " in "; \
		text += DADDR(2); \
		text += " counter "; \
		text += DADDR(1); \
		text += " exit "; \
		text += target(_code_ptr[ip + 4]); \
		incr += 5; \
	} break

#define DISASSEMBLE_ITERATE_TYPES(m_macro) \
	m_macro(INT); \
	m_macro(FLOAT); \
	m_macro(VECTOR2); \
	m_macro(VECTOR2I); \
	m_macro(VECTOR3); \
	m_macro(VECTOR3I); \
	m_macro(STRING); \
	m_macro(DICTIONARY); \
	m_macro(ARRAY); \
	m_macro(PACKED_BYTE_ARRAY); \
	m_macro(PACKED_INT32_ARRAY); \
	m_macro(PACKED_INT64_ARRAY); \
	m_macro(PACKED_FLOAT32_ARRAY); \
	m_macro(PACKED_FLOAT64_ARRAY); \
	m_macro(PACKED_STRING_ARRAY); \
	m_macro(PACKED_VECTOR2_ARRAY); \
	m_macro(PACKED_VECTOR3_ARRAY); \
	m_macro(PACKED_COLOR_ARRAY); \
	m_macro(PACKED_VECTOR4_ARRAY); \
	m_macro(OBJECT)

			case OPCODE_ITERATE_BEGIN: {
				op = "for-init";
				text += DADDR(3);
				text += " in ";
				text += DADDR(2);
				text += " counter ";
				text += DADDR(1);
				text += " exit ";
				text += target(_code_ptr[ip + 4]);

				incr += 5;
			} break;
				DISASSEMBLE_ITERATE_TYPES(DISASSEMBLE_ITERATE_BEGIN);
			case OPCODE_ITERATE_BEGIN_RANGE: {
				op = "for-init";
				text += DADDR(5);
				text += " in range from ";
				text += DADDR(2);
				text += " to ";
				text += DADDR(3);
				text += " step ";
				text += DADDR(4);
				text += " counter ";
				text += DADDR(1);
				text += " exit ";
				text += target(_code_ptr[ip + 6]);

				incr += 7;
			} break;
			case OPCODE_ITERATE: {
				op = "for-loop";
				text += DADDR(3);
				text += " in ";
				text += DADDR(2);
				text += " counter ";
				text += DADDR(1);
				text += " exit ";
				text += target(_code_ptr[ip + 4]);

				incr += 5;
			} break;
				DISASSEMBLE_ITERATE_TYPES(DISASSEMBLE_ITERATE);
			case OPCODE_ITERATE_RANGE: {
				op = "for-loop";
				text += DADDR(4);
				text += " in range to ";
				text += DADDR(2);
				text += " step ";
				text += DADDR(3);
				text += " counter ";
				text += DADDR(1);
				text += " exit ";
				text += target(_code_ptr[ip + 5]);

				incr += 6;
			} break;
			case OPCODE_STORE_GLOBAL: {
				op = "store global";
				text += DADDR(1);
				text += " = ";
				text += String::num_int64(_code_ptr[ip + 2]);

				incr += 3;
			} break;
			case OPCODE_STORE_NAMED_GLOBAL: {
				op = "store named global";
				text += DADDR(1);
				text += " = ";
				text += String(_global_names_ptr[_code_ptr[ip + 2]]);

				incr += 3;
			} break;
			case OPCODE_LINE: {
				int line = _code_ptr[ip + 1] - 1;
				if (line >= 0 && line < p_code_lines.size()) {
					is_line_marker = true;
					text += "; line ";
					text += itos(line + 1);
					text += ":  ";
					text += p_code_lines[line].strip_edges();
				}

				incr += 2;
			} break;

#define DISASSEMBLE_TYPE_ADJUST(m_v_type) \
	case OPCODE_TYPE_ADJUST_##m_v_type: { \
		op = "type adjust"; \
		text += "("; \
		text += #m_v_type; \
		text += ") "; \
		text += DADDR(1); \
		incr += 2; \
	} break

				DISASSEMBLE_TYPE_ADJUST(BOOL);
				DISASSEMBLE_TYPE_ADJUST(INT);
				DISASSEMBLE_TYPE_ADJUST(FLOAT);
				DISASSEMBLE_TYPE_ADJUST(STRING);
				DISASSEMBLE_TYPE_ADJUST(VECTOR2);
				DISASSEMBLE_TYPE_ADJUST(VECTOR2I);
				DISASSEMBLE_TYPE_ADJUST(RECT2);
				DISASSEMBLE_TYPE_ADJUST(RECT2I);
				DISASSEMBLE_TYPE_ADJUST(VECTOR3);
				DISASSEMBLE_TYPE_ADJUST(VECTOR3I);
				DISASSEMBLE_TYPE_ADJUST(TRANSFORM2D);
				DISASSEMBLE_TYPE_ADJUST(VECTOR4);
				DISASSEMBLE_TYPE_ADJUST(VECTOR4I);
				DISASSEMBLE_TYPE_ADJUST(PLANE);
				DISASSEMBLE_TYPE_ADJUST(QUATERNION);
				DISASSEMBLE_TYPE_ADJUST(AABB);
				DISASSEMBLE_TYPE_ADJUST(BASIS);
				DISASSEMBLE_TYPE_ADJUST(TRANSFORM3D);
				DISASSEMBLE_TYPE_ADJUST(PROJECTION);
				DISASSEMBLE_TYPE_ADJUST(COLOR);
				DISASSEMBLE_TYPE_ADJUST(STRING_NAME);
				DISASSEMBLE_TYPE_ADJUST(NODE_PATH);
				DISASSEMBLE_TYPE_ADJUST(RID);
				DISASSEMBLE_TYPE_ADJUST(OBJECT);
				DISASSEMBLE_TYPE_ADJUST(CALLABLE);
				DISASSEMBLE_TYPE_ADJUST(SIGNAL);
				DISASSEMBLE_TYPE_ADJUST(DICTIONARY);
				DISASSEMBLE_TYPE_ADJUST(ARRAY);
				DISASSEMBLE_TYPE_ADJUST(PACKED_BYTE_ARRAY);
				DISASSEMBLE_TYPE_ADJUST(PACKED_INT32_ARRAY);
				DISASSEMBLE_TYPE_ADJUST(PACKED_INT64_ARRAY);
				DISASSEMBLE_TYPE_ADJUST(PACKED_FLOAT32_ARRAY);
				DISASSEMBLE_TYPE_ADJUST(PACKED_FLOAT64_ARRAY);
				DISASSEMBLE_TYPE_ADJUST(PACKED_STRING_ARRAY);
				DISASSEMBLE_TYPE_ADJUST(PACKED_VECTOR2_ARRAY);
				DISASSEMBLE_TYPE_ADJUST(PACKED_VECTOR3_ARRAY);
				DISASSEMBLE_TYPE_ADJUST(PACKED_COLOR_ARRAY);
				DISASSEMBLE_TYPE_ADJUST(PACKED_VECTOR4_ARRAY);

			case OPCODE_ASSERT: {
				op = "assert";
				text += "(";
				text += DADDR(1);
				text += ", ";
				text += DADDR(2);
				text += ")";

				incr += 3;
			} break;
			case OPCODE_BREAKPOINT: {
				op = "breakpoint";

				incr += 1;
			} break;
			case OPCODE_END: {
				op = "== END ==";

				incr += 1;
			} break;
		}

		ip += incr;
		if (is_line_marker) {
			row_ips.push_back(instr_ip);
			row_texts.push_back(text.as_string());
			row_markers.push_back(true);
		} else if (!op.is_empty()) {
			row_ips.push_back(instr_ip);
			row_texts.push_back(text.get_string_length() > 0 ? op.rpad(36) + text.as_string() : op);
			row_markers.push_back(false);
		}
	}

	for (int i = 0; i < row_texts.size(); i++) {
		const bool is_target = targets.has(row_ips[i]);
		if (row_markers[i]) {
			out += "\n      ";
			out += is_target ? " > " : "   ";
			out += row_texts[i];
			out += "\n";
		} else {
			out += itos(row_ips[i]).lpad(6);
			out += is_target ? " > " : "   ";
			out += row_texts[i];
			out += "\n";
		}
	}

	return out.as_string();
}

void GDScriptFunction::disassemble(const Vector<String>& p_code_lines) const {
	print_line(get_disassembly(p_code_lines).strip_edges(false, true));
}

static Vector<String> _get_script_source_lines(const GDScript* p_script) {
	const GDScript* root = const_cast<GDScript*>(p_script)->get_root_script();
	if (!root->has_source_code()) {
		return Vector<String>();
	}
	return root->get_source_code().split("\n", true);
}

String GDScript::get_function_disassembly(const StringName& p_function) const {
	const GDScriptFunction* function = nullptr;

	HashMap<StringName, GDScriptFunction*>::ConstIterator found = member_functions.find(p_function);
	if (found) {
		function = found->value;
	} else if (implicit_initializer && implicit_initializer->get_name() == p_function) {
		function = implicit_initializer;
	} else if (implicit_ready && implicit_ready->get_name() == p_function) {
		function = implicit_ready;
	} else if (static_initializer && static_initializer->get_name() == p_function) {
		function = static_initializer;
	}

	if (!function) {
		return String();
	}

	return function->get_disassembly(_get_script_source_lines(this));
}

String GDScript::get_disassembly() const {
	Vector<String> lines = _get_script_source_lines(this);
	StringBuilder out;

	out += "; ######## script ";
	out += debug_get_script_name(Ref<Script>(const_cast<GDScript*>(this)));
	out += "\n\n";

	if (static_initializer) {
		out += static_initializer->get_disassembly(lines);
		out += "\n";
	}
	if (implicit_initializer) {
		out += implicit_initializer->get_disassembly(lines);
		out += "\n";
	}
	if (implicit_ready) {
		out += implicit_ready->get_disassembly(lines);
		out += "\n";
	}

	Vector<StringName> function_names;
	for (const KeyValue<StringName, GDScriptFunction*>& kv : member_functions) {
		function_names.push_back(kv.key);
	}
	function_names.sort_custom<StringName::AlphCompare>();

	for (const StringName& function_name : function_names) {
		out += member_functions.get(function_name)->get_disassembly(lines);
		out += "\n";
	}

	Vector<StringName> subclass_names;
	for (const KeyValue<StringName, Ref<GDScript>>& kv : subclasses) {
		subclass_names.push_back(kv.key);
	}
	subclass_names.sort_custom<StringName::AlphCompare>();

	for (const StringName& subclass_name : subclass_names) {
		out += subclasses.get(subclass_name)->get_disassembly();
	}

	return out.as_string();
}

#endif // DEBUG_ENABLED