/**************************************************************************/
/*  gdscript_refactor.h                                                   */
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
/// Hey! This file was created for the Reginleif fork! It's not part of Godot upstream.
/// Copyright (c) 2026 chesedcore (Monarch).
/// Licensed under the MIT License, same terms as the Godot engine.

#pragma once

#include "gdscript_refactor.h"

#ifdef TOOLS_ENABLED

#include "gdscript_analyzer.h"
#include "gdscript_parser.h"

#include "core/config/project_settings.h"
#include "core/object/class_db.h"
#include "core/variant/variant.h"
#include "core/templates/hash_set.h"

using GP = GDScriptParser;

///0 is indents, 1 is braces. see registered in GDScriptLanguage::init()
static bool _use_braces() {
	ProjectSettings* ps = ProjectSettings::get_singleton();
	return ps != nullptr && int(ps->get_setting("reginleif/editor/scope_style", 0)) == 1;
}

namespace {

///small helpers
String _type_to_string(const GP::TypeNode* p_type) {
	if (p_type == nullptr) {
		return String();
	}

	if (!p_type->trait_bounds.is_empty()) {
		String s = "impl ";
		for (int i = 0; i < p_type->trait_bounds.size(); i++) {
			if (i > 0) {
				s += " + ";
			}
			s += String(p_type->trait_bounds[i]->name);
		}
		return s;
	}

	if (p_type->type_chain.is_empty()) {
		return "void";
	}

	String s;
	for (uint32_t i = 0; i < p_type->type_chain.size(); i++) {
		if (i > 0) {
			s += ".";
		}
		s += String(p_type->type_chain[i]->name);
	}

	if (!p_type->container_types.is_empty()) {
		s += "[";
		for (uint32_t i = 0; i < p_type->container_types.size(); i++) {
			if (i > 0) {
				s += ", ";
			}
			s += _type_to_string(p_type->container_types[i]);
		}
		s += "]";
	}
	return s;
}

String _leading_whitespace(const String& p_line) {
	int i = 0;
	while (i < p_line.length() && (p_line[i] == ' ' || p_line[i] == '\t')) {
		i++;
	}

	return p_line.substr(0, i);
}


bool _is_blank_or_comment(const String& p_line) {
	const String s = p_line.strip_edges();
	return s.is_empty() || s.begins_with("#");
}

int _eff_start(const GP::Node* p_node) {
	int start = p_node->start_line;
	for (const GP::AnnotationNode* annotation : p_node->annotations) {
		start = MIN(start, annotation->start_line);
	}

	return start;
}

int _eff_end(const GP::Node* p_node) {
	return p_node->end_line;
}

bool _is_numeric_name(const String& p_t) {
	return p_t == "int" || p_t == "float";
}

///most of this is rewritten guesswork from the analyser, yay!
String _infer_type(const GP::ExpressionNode* p_expr) {
	if (p_expr == nullptr) {
		return String();
	}

	switch (p_expr->type) {
		case GP::Node::LITERAL: {
			const Variant::Type t = static_cast<const GP::LiteralNode*>(p_expr)->value.get_type();
			if (t == Variant::NIL || t == Variant::OBJECT) {
				return String();
			}
			return Variant::get_type_name(t);
		}
		case GP::Node::ARRAY:
			return "Array";
		case GP::Node::DICTIONARY:
			return "Dictionary";
		case GP::Node::UNARY_OPERATOR: {
			const GP::UnaryOpNode* n = static_cast<const GP::UnaryOpNode*>(p_expr);
			if (n->operation == GP::UnaryOpNode::OP_LOGIC_NOT) {
				return "bool";
			}
			const String t = _infer_type(n->operand);
			return (t == "int" || (t == "float" && n->operation != GP::UnaryOpNode::OP_COMPLEMENT)) ? t : String();
		}
		case GP::Node::BINARY_OPERATOR: {
			const GP::BinaryOpNode* n = static_cast<const GP::BinaryOpNode*>(p_expr);
			switch (n->operation) {
				case GP::BinaryOpNode::OP_LOGIC_AND:
				case GP::BinaryOpNode::OP_LOGIC_OR:
				case GP::BinaryOpNode::OP_CONTENT_TEST:
				case GP::BinaryOpNode::OP_COMP_EQUAL:
				case GP::BinaryOpNode::OP_COMP_NOT_EQUAL:
				case GP::BinaryOpNode::OP_COMP_LESS:
				case GP::BinaryOpNode::OP_COMP_LESS_EQUAL:
				case GP::BinaryOpNode::OP_COMP_GREATER:
				case GP::BinaryOpNode::OP_COMP_GREATER_EQUAL:
					return "bool";
				case GP::BinaryOpNode::OP_BIT_LEFT_SHIFT:
				case GP::BinaryOpNode::OP_BIT_RIGHT_SHIFT:
				case GP::BinaryOpNode::OP_BIT_AND:
				case GP::BinaryOpNode::OP_BIT_OR:
				case GP::BinaryOpNode::OP_BIT_XOR:
					return "int";
				default:
					break;
			}
			const String l = _infer_type(n->left_operand);
			const String r = _infer_type(n->right_operand);
			if (l.is_empty() || r.is_empty()) {
				return String();
			}

			if (n->operation == GP::BinaryOpNode::OP_MODULO && l == "String") {
				return "String"; ///`"fmt %s" % x`
			}
			if (_is_numeric_name(l) && _is_numeric_name(r)) {
				return (l == "float" || r == "float") ? String("float") : String("int");
			}

			if (l == r && l != "bool" && n->operation != GP::BinaryOpNode::OP_POWER) {
				return l;
			}
			return String();
		}

		case GP::Node::TERNARY_OPERATOR: {
			const GP::TernaryOpNode* n = static_cast<const GP::TernaryOpNode*>(p_expr);
			const String a = _infer_type(n->true_expr);
			return (!a.is_empty() && a == _infer_type(n->false_expr)) ? a : String();
		}
		case GP::Node::CAST: {
			const GP::CastNode* n = static_cast<const GP::CastNode*>(p_expr);
			return _type_to_string(n->cast_type);
		}

		case GP::Node::TYPE_TEST:
			return "bool";
		case GP::Node::CALL: {
			const GP::CallNode* n = static_cast<const GP::CallNode*>(p_expr);

			if (n->callee == nullptr) {
				return String();
			}

			if (n->callee->type == GP::Node::IDENTIFIER) {
				const GP::IdentifierNode* callee = static_cast<const GP::IdentifierNode*>(n->callee);
				if (callee->source != GP::IdentifierNode::UNDEFINED_SOURCE) {
					return String();
				}

				const String name = String(callee->name);
				const Variant::Type t = Variant::get_type_by_name(name);
				if (t != Variant::VARIANT_MAX && t != Variant::NIL && t != Variant::OBJECT) {
					return name;
				}
				if (name == "str") {
					return "String";
				}
				if (name == "len" || name == "randi") {
					return "int";
				}
				if (name == "randf") {
					return "float";
				}

			} else if (n->callee->type == GP::Node::SUBSCRIPT) {
				const GP::SubscriptNode* sub = static_cast<const GP::SubscriptNode*>(n->callee);
				if (sub->is_attribute && sub->attribute != nullptr && sub->attribute->name == StringName("new") && sub->base != nullptr && sub->base->type == GP::Node::IDENTIFIER) {
					const GP::IdentifierNode* base = static_cast<const GP::IdentifierNode*>(sub->base);
					if (base->source == GP::IdentifierNode::UNDEFINED_SOURCE && ClassDB::class_exists(base->name) && ClassDB::can_instantiate(base->name)) {
						return String(base->name); ///Node.new()
					}
				}
			}
			return String();

		}
		default:
			return String();
	}
}

struct TypeCtx {
	bool analyzed = false;
	const GP::ClassNode* owner = nullptr; ///the class the extracted fn will be created in
};

String _datatype_to_source(const GP::DataType& p_type, const GP::ClassNode* p_owner, bool p_top) {
	if (!p_type.is_set() || p_type.is_meta_type || p_type.is_pseudo_type) {
		return String();
	}

	if (p_top && !p_type.is_hard_type()) {
		return String();
	}

	switch (p_type.kind) {
		case GP::DataType::BUILTIN: {
			if (p_type.builtin_type == Variant::NIL) {
				return String();
			}

			const String name = Variant::get_type_name(p_type.builtin_type);
			if (p_type.builtin_type == Variant::ARRAY && p_type.has_container_element_type(0)) {
				const String e = _datatype_to_source(p_type.get_container_element_type(0), p_owner, false);
				return e.is_empty() ? name : vformat("Array[%s]", e);
			}

			if (p_type.builtin_type == Variant::DICTIONARY && p_type.has_container_element_types()) {
				const String k = _datatype_to_source(p_type.get_container_element_type_or_variant(0), p_owner, false);
				const String v = _datatype_to_source(p_type.get_container_element_type_or_variant(1), p_owner, false);
				return (k.is_empty() || v.is_empty()) ? name : vformat("Dictionary[%s, %s]", k, v);
			}
			return name;
		}
		case GP::DataType::NATIVE: {
			if (p_type.native_type == StringName() || !ClassDB::class_exists(p_type.native_type)) {
				return String();
			}
			return String(p_type.native_type);
		}
		case GP::DataType::CLASS: {
			const GP::ClassNode* c = p_type.class_type;
			if (c == nullptr || c->identifier == nullptr || !p_type.generic_type_bindings.is_empty()) {
				return String();
			}

			const StringName name = c->identifier->name;
			if (c->outer == nullptr) {
				return ScriptServer::is_global_class(name) ? String(name) : String();
			}

			for (const GP::ClassNode* o = p_owner; o != nullptr; o = o->outer) {
				if (o == c) {
					return String(name);
				}
				if (o->has_member(name)) {
					const GP::ClassNode::Member m = o->get_member(name);
					if (m.type == GP::ClassNode::Member::CLASS && m.m_class == c) {
						return String(name);
					}
				}
			}
			return String();
		}
		case GP::DataType::SCRIPT: {
			if (p_type.script_type.is_valid()) {
				const StringName g = p_type.script_type->get_global_name();
				if (g != StringName() && ScriptServer::is_global_class(g)) {
					return String(g); ///??? ik lol
				}
			}
			return String();
		}
		default:
			return String(); ///VARIANT, ENUM, GENERIC_TYPE, TRAIT_OBJECT, unresolved
	}
}

String _best_type(const TypeCtx& p_ctx, const GP::DataType& p_analyzed, const GP::TypeNode* p_specifier, bool p_infer, const GP::ExpressionNode* p_initializer) {
	if (p_specifier != nullptr) {
		return _type_to_string(p_specifier);
	}
	if (p_ctx.analyzed) {
		return _datatype_to_source(p_analyzed, p_ctx.owner, true);
	}
	return p_infer ? _infer_type(p_initializer) : String();
}

struct LocalInfo {
	const void* decl = nullptr;
	StringName name;
	int line = 0;
	int column = 0;
	String type_text; ///only if explicitly annotated
	bool is_constant = false;
	bool declared_inside = false;
};

struct LocalLess {
	bool operator()(const LocalInfo& p_a, const LocalInfo& p_b) const {
		if (p_a.line != p_b.line) {
			return p_a.line < p_b.line;
		}

		return p_a.column < p_b.column;
	}
};

bool _local_from_identifier(const GP::IdentifierNode* p_id, const TypeCtx& p_ctx, LocalInfo& r_info) {
	r_info.name = p_id->name;
	switch (p_id->source) {
		case GP::IdentifierNode::FUNCTION_PARAMETER: {
			const GP::ParameterNode* p = p_id->parameter_source;
			if (p == nullptr) {
				return false;
			}

			r_info.decl = p;
			r_info.line = p->start_line;
			r_info.column = p->start_column;
			r_info.type_text = _best_type(p_ctx, p->type_constraint, p->datatype_specifier, p->infer_datatype, p->initializer);
			return true;
		}

		case GP::IdentifierNode::LOCAL_VARIABLE: {
			const GP::VariableNode* v = p_id->variable_source;
			if (v == nullptr) {
				return false;
			}
			r_info.decl = v;
			r_info.line = v->start_line;
			r_info.column = v->start_column;
			r_info.type_text = _best_type(p_ctx, v->type_constraint, v->datatype_specifier, v->infer_datatype, v->initializer);
			return true;
		}
		case GP::IdentifierNode::LOCAL_CONSTANT: {
			const GP::ConstantNode* c = p_id->constant_source;
			if (c == nullptr) {
				return false;
			}
			r_info.decl = c;
			r_info.line = c->start_line;
			r_info.column = c->start_column;
			r_info.type_text = _best_type(p_ctx, c->type_constraint, c->datatype_specifier, c->infer_datatype, c->initializer);
			r_info.is_constant = true;
			return true;
		}
		case GP::IdentifierNode::LOCAL_ITERATOR:
		case GP::IdentifierNode::LOCAL_BIND: {
			const GP::IdentifierNode* b = p_id->bind_source;
			if (b == nullptr) {
				return false;
			}
			r_info.decl = b;
			r_info.line = b->start_line;
			r_info.column = b->start_column;
			r_info.type_text = _best_type(p_ctx, b->type_constraint, nullptr, false, nullptr);
			return true;
		}
		default:
			return false;
	}
}

bool _local_from_assignable(const GP::AssignableNode* p_node, bool p_is_constant, const TypeCtx& p_ctx, LocalInfo& r_info) {
	if (p_node == nullptr || p_node->identifier == nullptr) {
		return false;
	}

	r_info.decl = p_node;
	r_info.name = p_node->identifier->name;
	r_info.line = p_node->start_line;
	r_info.column = p_node->start_column;
	r_info.type_text = _best_type(p_ctx, p_node->type_constraint, p_node->datatype_specifier, p_node->infer_datatype, p_node->initializer);
	r_info.is_constant = p_is_constant;
	return true;
}

struct WalkState {
	int lambda_depth = 0;
	int loop_depth = 0; ///loops entered since the walk started
};

///preorder
template <typename TVisit>
void _walk(const GP::Node* p_node, WalkState& p_state, TVisit& p_visit) {
	if (p_node == nullptr) {
		return;
	}

	p_visit(p_node, p_state);

	switch (p_node->type) {
		case GP::Node::ARRAY: {
			const GP::ArrayNode* n = static_cast<const GP::ArrayNode*>(p_node);
			for (const GP::ExpressionNode* e : n->elements) {
				_walk(e, p_state, p_visit);
			}
		} break;
		case GP::Node::ASSERT: {
			const GP::AssertNode* n = static_cast<const GP::AssertNode*>(p_node);
			_walk(n->condition, p_state, p_visit);
			_walk(n->message, p_state, p_visit);
		} break;
		case GP::Node::ASSIGNMENT: {
			const GP::AssignmentNode* n = static_cast<const GP::AssignmentNode*>(p_node);
			_walk(n->assignee, p_state, p_visit);
			_walk(n->assigned_value, p_state, p_visit);
		} break;
		case GP::Node::AWAIT: {
			const GP::AwaitNode* n = static_cast<const GP::AwaitNode*>(p_node);
			_walk(n->to_await, p_state, p_visit);
		} break;
		case GP::Node::BINARY_OPERATOR: {
			const GP::BinaryOpNode* n = static_cast<const GP::BinaryOpNode*>(p_node);
			_walk(n->left_operand, p_state, p_visit);
			_walk(n->right_operand, p_state, p_visit);
		} break;

		case GP::Node::CALL: {
			const GP::CallNode* n = static_cast<const GP::CallNode*>(p_node);
			if (n->callee != nullptr && n->callee->type == GP::Node::SUBSCRIPT) {
				_walk(n->callee, p_state, p_visit);
			}
			for (const GP::ExpressionNode* a : n->arguments) {
				_walk(a, p_state, p_visit);
			}
		} break;
		case GP::Node::CAST: {
			const GP::CastNode* n = static_cast<const GP::CastNode*>(p_node);
			_walk(n->operand, p_state, p_visit);
		} break;
		case GP::Node::DICTIONARY: {
			const GP::DictionaryNode* n = static_cast<const GP::DictionaryNode*>(p_node);
			for (const GP::DictionaryNode::Pair& pair : n->elements) {
				const bool lua_key = n->style == GP::DictionaryNode::LUA_TABLE && pair.key != nullptr && pair.key->type == GP::Node::IDENTIFIER;
				if (!lua_key) {
					_walk(pair.key, p_state, p_visit);
				}
				_walk(pair.value, p_state, p_visit);
			}
		} break;
		case GP::Node::LAMBDA: {
			const GP::LambdaNode* n = static_cast<const GP::LambdaNode*>(p_node);
			if (n->function != nullptr) {
				for (const GP::ParameterNode* p : n->function->parameters) {
					_walk(p->initializer, p_state, p_visit);
				}
				const int saved_loop_depth = p_state.loop_depth;
				p_state.loop_depth = 0;
				p_state.lambda_depth++;
				_walk(n->function->body, p_state, p_visit);
				p_state.lambda_depth--;
				p_state.loop_depth = saved_loop_depth;
			}
		} break;
		case GP::Node::SUBSCRIPT: {
			const GP::SubscriptNode* n = static_cast<const GP::SubscriptNode*>(p_node);
			_walk(n->base, p_state, p_visit);
			if (!n->is_attribute) {
				_walk(n->index, p_state, p_visit);
			}
		} break;

		case GP::Node::TERNARY_OPERATOR: {
			const GP::TernaryOpNode* n = static_cast<const GP::TernaryOpNode*>(p_node);
			_walk(n->condition, p_state, p_visit);
			_walk(n->true_expr, p_state, p_visit);
			_walk(n->false_expr, p_state, p_visit);
		} break;

		case GP::Node::TYPE_TEST: {
			const GP::TypeTestNode* n = static_cast<const GP::TypeTestNode*>(p_node);
			_walk(n->operand, p_state, p_visit);
		} break;
		case GP::Node::UNARY_OPERATOR: {
			const GP::UnaryOpNode* n = static_cast<const GP::UnaryOpNode*>(p_node);
			_walk(n->operand, p_state, p_visit);
		} break;
		case GP::Node::FOR: {
			const GP::ForNode* n = static_cast<const GP::ForNode*>(p_node);
			_walk(n->list, p_state, p_visit);
			p_state.loop_depth++;
			_walk(n->loop, p_state, p_visit);
			p_state.loop_depth--;
		} break;
		case GP::Node::WHILE: {
			const GP::WhileNode* n = static_cast<const GP::WhileNode*>(p_node);
			_walk(n->condition, p_state, p_visit);
			p_state.loop_depth++;
			_walk(n->loop, p_state, p_visit);
			p_state.loop_depth--;
		} break;
		case GP::Node::IF: {
			const GP::IfNode* n = static_cast<const GP::IfNode*>(p_node);
			_walk(n->condition, p_state, p_visit);
			_walk(n->true_block, p_state, p_visit);
			_walk(n->false_block, p_state, p_visit);
		} break;
		case GP::Node::MATCH: {
			const GP::MatchNode* n = static_cast<const GP::MatchNode*>(p_node);
			_walk(n->test, p_state, p_visit);
			for (const GP::MatchBranchNode* b : n->branches) {
				_walk(b, p_state, p_visit);
			}
		} break;

		case GP::Node::MATCH_BRANCH: {
			const GP::MatchBranchNode* n = static_cast<const GP::MatchBranchNode*>(p_node);
			for (const GP::PatternNode* pattern : n->patterns) {
				_walk(pattern, p_state, p_visit);
			}

			_walk(n->guard_body, p_state, p_visit);
			_walk(n->block, p_state, p_visit);
		} break;

		case GP::Node::PATTERN: {
			const GP::PatternNode* n = static_cast<const GP::PatternNode*>(p_node);
			switch (n->pattern_type) {
				case GP::PatternNode::PT_EXPRESSION:
					_walk(n->expression, p_state, p_visit);
					break;
				case GP::PatternNode::PT_ARRAY:
					for (const GP::PatternNode* sub : n->array) {
						_walk(sub, p_state, p_visit);
					}
					break;

				case GP::PatternNode::PT_DICTIONARY:
					for (const GP::PatternNode::Pair& pair : n->dictionary) {
						_walk(pair.key, p_state, p_visit);
						_walk(pair.value_pattern, p_state, p_visit);
					}
					break;
				default:
					break;
			}

		} break;

		case GP::Node::RETURN: {
			const GP::ReturnNode* n = static_cast<const GP::ReturnNode*>(p_node);
			_walk(n->return_value, p_state, p_visit);
		} break;

		case GP::Node::SUITE: {
			const GP::SuiteNode* n = static_cast<const GP::SuiteNode*>(p_node);
			for (const GP::Node* statement : n->statements) {
				_walk(statement, p_state, p_visit);
			}
		} break;

		case GP::Node::VARIABLE: {
			const GP::VariableNode* n = static_cast<const GP::VariableNode*>(p_node);
			_walk(n->initializer, p_state, p_visit);
		} break;
		case GP::Node::CONSTANT: {
			const GP::ConstantNode* n = static_cast<const GP::ConstantNode*>(p_node);
			_walk(n->initializer, p_state, p_visit);
		} break;
		default:
			break; ///IDENTIFIER, LITERAL, SELF, GET_NODE, PRELOAD, BREAK, CONTINUE, PASS, BREAKPOINT
	}
}

struct Selection {
	const GP::SuiteNode* suite = nullptr;
	uint32_t first = 0;
	uint32_t last = 0; ///inclusive
	bool in_loop = false;
};

enum LocateResult {
	LOC_OK,
	LOC_NONE,    ///no statement of this suite touches that range
	LOC_CROSSES, ///this range cuts through a statement
};

///here we fucking go again. switch case hell
void _child_suites(const GP::Node* p_statement, Vector<const GP::SuiteNode*>& r_suites, bool& r_is_loop) {
	r_is_loop = false;

	switch (p_statement->type) {
		case GP::Node::IF: {
			const GP::IfNode* n = static_cast<const GP::IfNode*>(p_statement);
			if (n->true_block != nullptr) {
				r_suites.push_back(n->true_block);
			}
			if (n->false_block != nullptr) {
				r_suites.push_back(n->false_block);
			}

		} break;

		case GP::Node::FOR: {
			const GP::ForNode* n = static_cast<const GP::ForNode*>(p_statement);
			r_is_loop = true;
			if (n->loop != nullptr) {
				r_suites.push_back(n->loop);
			}
		} break;

		case GP::Node::WHILE: {
			const GP::WhileNode* n = static_cast<const GP::WhileNode*>(p_statement);
			r_is_loop = true;
			if (n->loop != nullptr) {
				r_suites.push_back(n->loop);
			}
		} break;

		case GP::Node::MATCH: {
			const GP::MatchNode* n = static_cast<const GP::MatchNode*>(p_statement);
			for (const GP::MatchBranchNode* branch : n->branches) {
				if (branch != nullptr && branch->block != nullptr) {
					r_suites.push_back(branch->block);
				}
			}
		} break;
		default:
			break;
	}
}

LocateResult _locate(const GP::SuiteNode* p_suite, int p_first, int p_last, bool p_in_loop, Selection& r_selection_fuck_my_ass_i_guess) {
	Vector<uint32_t> overlapping;
	for (uint32_t i = 0; i < p_suite->statements.size(); i++) {
		const GP::Node* s = p_suite->statements[i];
		if (s == nullptr || s->start_line < 0) {
			continue;
		}
		if (_eff_end(s) >= p_first && _eff_start(s) <= p_last) {
			overlapping.push_back(i);
		}
	}

	if (overlapping.is_empty()) {
		return LOC_NONE;
	}

	///fun fact i fucking slept off while trying to write the 900th mini parser switch case machine
	bool all_inside = true;
	for (uint32_t idx : overlapping) {

		const GP::Node* s = p_suite->statements[idx];

		if (_eff_start(s) < p_first || _eff_end(s) > p_last) {
			all_inside = false;
			break;
		}

	}

	if (all_inside) {
		r_selection_fuck_my_ass_i_guess.suite = p_suite;
		r_selection_fuck_my_ass_i_guess.first = overlapping[0];
		r_selection_fuck_my_ass_i_guess.last = overlapping[overlapping.size() - 1];
		r_selection_fuck_my_ass_i_guess.in_loop = p_in_loop;
		return LOC_OK;
	}

	if (overlapping.size() != 1) {
		return LOC_CROSSES;
	}

	///try cut selection from the blocks
	const GP::Node* s = p_suite->statements[overlapping[0]];
	Vector<const GP::SuiteNode*> children;
	bool is_loop = false;

	_child_suites(s, children, is_loop);
	for (const GP::SuiteNode* child : children) {
		const LocateResult r = _locate(child, p_first, p_last, p_in_loop || is_loop, r_selection_fuck_my_ass_i_guess);
		if (r != LOC_NONE) {
			return r;
		}
	}
	return LOC_CROSSES;
}

const GP::FunctionNode* _find_function(const GP::ClassNode* p_class, int p_first, int p_last, const GP::ClassNode*& r_class) {

	for (uint32_t i = 0; i < p_class->members.size(); i++) {
		const GP::ClassNode::Member& m = p_class->members[i];
		if (m.type == GP::ClassNode::Member::CLASS && m.m_class != nullptr) {
			const GP::FunctionNode* found = _find_function(m.m_class, p_first, p_last, r_class);
			if (found != nullptr) {
				return found;
			}

		} else if (m.type == GP::ClassNode::Member::FUNCTION && m.function != nullptr && m.function->identifier != nullptr && m.function->body != nullptr) {
			if (m.function->start_line <= p_first && m.function->end_line >= p_last) {
				r_class = p_class;
				return m.function;
			}
		}
	}

	return nullptr;
}


struct SelectionFacts {
	bool has_return = false;
	bool has_escaping_jump = false;
	bool has_super = false;
	bool has_await = false;

	Vector<LocalInfo> used;
	HashSet<const void*> used_set;
	Vector<LocalInfo> declared;
	HashSet<const void*> assigned;
	HashSet<int> verbatim_lines;
};

} // namespace

///////////////////////////////////////////////////////////////////////////////

GDScriptRefactor::ExtractResult GDScriptRefactor::extract_function(const String& p_source, const String& p_path, int p_first_line, int p_last_line, const String& p_name) {
	ExtractResult res;

	Vector<String> errors;
	auto bail = [&]() -> ExtractResult {
		res.ok = false;
		res.edits.clear();
		res.error = errors.size() == 1 ? errors[0] : String("- ") + String("\n- ").join(errors);
		return res;
	};

	auto fail = [&](const String& p_message) -> ExtractResult {
		errors.push_back(p_message);
		return bail();
	};

	if (p_first_line > p_last_line) {
		SWAP(p_first_line, p_last_line);
	}

	if (!p_name.is_valid_unicode_identifier()) {
		errors.push_back(vformat(R"("%s" is not a valid function name.)", p_name));
	}

	GDScriptParser parser;
	if (parser.parse(p_source, p_path, false, true) != OK) {
		return fail("The script has errors. Try fixing them before extracting any functions.");
	}
	if (parser.is_trait_script() || parser.get_tree() == nullptr) {
		return fail("Extracting a function is only supported in class scripts for now, sorry ^^'");
	}

	GDScriptAnalyzer analyzer(&parser);
	TypeCtx ctx;
	ctx.analyzed = analyzer.analyze() == OK;

	const Vector<String> lines = p_source.split("\n", true);
	if (p_first_line < 0 || p_last_line >= lines.size()) {
		return fail("The selection is outside of the script.");
	}

	const int first1 = p_first_line + 1;
	const int last1 = p_last_line + 1;

	///1. which function is my ass in?
	const GP::ClassNode* owner_class = nullptr;
	const GP::FunctionNode *fn = _find_function(parser.get_tree(), first1, last1, owner_class);
	if (fn == nullptr || owner_class == nullptr) {
		return fail("Select statements inside a function body.");
	}

	ctx.owner = owner_class;
	if (fn->has_generic_parameters()) {
		errors.push_back("Extracting from a function with generic parameters is not supported yet!");
	}

	if (!fn->has_explicit_body || fn->body->statements.is_empty()) {
		return fail("This function has no body.");
	}

	///2. is my ass' name colliding with another ass?
	if (owner_class->has_member(p_name)) {
		errors.push_back(vformat(R"(This class already has a member named "%s".)", p_name));
	}
	{
		const StringName native_base = owner_class->extends.is_empty() ? StringName("RefCounted") : owner_class->extends[0]->name;
		if (ClassDB::class_exists(native_base) && ClassDB::has_method(native_base, p_name)) {
			errors.push_back(vformat(R"("%s" is already a method of "%s".)", p_name, String(native_base)));
		}
	}

	///3. which statements did my ass select?
	Selection sel;
	const LocateResult located = _locate(fn->body, first1, last1, false, sel);
	if (located == LOC_NONE) {
		return fail("The selection does not contain any statement.");
	}
	if (located == LOC_CROSSES) {
		return fail("The selection must cover whole statements inside a single block.");
	}

	const GP::Node* first_stmt = sel.suite->statements[sel.first];
	const GP::Node* last_stmt = sel.suite->statements[sel.last];
	const int sel_first1 = _eff_start(first_stmt);
	const int sel_last1 = _eff_end(last_stmt);

	if (sel_first1 <= fn->header_end_line) {
		errors.push_back("The selection shares a line with the function header.");
	}
	if (first_stmt->type == GP::Node::IF && lines[sel_first1 - 1].strip_edges().begins_with("elif")) {
		errors.push_back("An elif branch can't be extracted on its own.");
	}

	bool random_fuckass_stray_code = false;
	for (int l = p_first_line; l < sel_first1 - 1 && !random_fuckass_stray_code; l++) {
		random_fuckass_stray_code = !_is_blank_or_comment(lines[l]);
	}

	for (int l = sel_last1; l <= p_last_line && !random_fuckass_stray_code; l++) {
		random_fuckass_stray_code = !_is_blank_or_comment(lines[l]);
	}

	if (random_fuckass_stray_code) {
		errors.push_back("The selection includes code that is not part of the selected statements (a block header, perhaps?)");
	}

	///4. my ass should no walk the selection
	SelectionFacts facts;
	{
		///i'm genuinely so fucking tired

		auto visit = [&facts, &ctx](const GP::Node* p_node, WalkState& p_state) {
			switch (p_node->type) {
				case GP::Node::IDENTIFIER: {
					LocalInfo li;
					if (_local_from_identifier(static_cast<const GP::IdentifierNode *>(p_node), ctx, li) && !facts.used_set.has(li.decl)) {
						facts.used_set.insert(li.decl);
						facts.used.push_back(li);
					}
				} break;

				case GP::Node::ASSIGNMENT: {
					const GP::ExpressionNode* root = static_cast<const GP::AssignmentNode*>(p_node)->assignee;

					while (root != nullptr && root->type == GP::Node::SUBSCRIPT) {
						root = static_cast<const GP::SubscriptNode*>(root)->base;
					}

					if (root != nullptr && root->type == GP::Node::IDENTIFIER) {
						LocalInfo li;
						if (_local_from_identifier(static_cast<const GP::IdentifierNode*>(root), ctx, li)) {
							facts.assigned.insert(li.decl);
						}
					}

				} break;
				case GP::Node::VARIABLE: {
					LocalInfo li;
					if (_local_from_assignable(static_cast<const GP::VariableNode*>(p_node), false, ctx, li)) {
						li.declared_inside = true;
						facts.declared.push_back(li);
					}
				} break;
				case GP::Node::CONSTANT: {
					LocalInfo li;
					if (_local_from_assignable(static_cast<const GP::ConstantNode*>(p_node), true, ctx, li)) {
						li.declared_inside = true;
						facts.declared.push_back(li);
					}
				} break;
				case GP::Node::RETURN:
					if (p_state.lambda_depth == 0) {
						facts.has_return = true;
					}
					break;
				case GP::Node::BREAK:
				case GP::Node::CONTINUE:
					if (p_state.lambda_depth == 0 && p_state.loop_depth == 0) {
						facts.has_escaping_jump = true;
					}
					break;
				case GP::Node::AWAIT:
					if (p_state.lambda_depth == 0) {
						facts.has_await = true;
					}
					break;
				case GP::Node::CALL:
					if (static_cast<const GP::CallNode*>(p_node)->is_super) {
						facts.has_super = true;
					}
					break;
				case GP::Node::LITERAL: {
					const GP::LiteralNode* literal = static_cast<const GP::LiteralNode*>(p_node);
					if (literal->value.get_type() == Variant::STRING && literal->end_line > literal->start_line) {
						for (int l = literal->start_line; l < literal->end_line; l++) {
							facts.verbatim_lines.insert(l);
						}
					}

				} break;
				default:
					break;
			}
		};

		WalkState state;
		for (uint32_t i = sel.first; i <= sel.last; i++) {
			_walk(sel.suite->statements[i], state, visit);
		}
	}

	if (facts.has_return) {
		errors.push_back(R"(The selection contains a "return". Extracting it would change what the function returns.)");
	}
	if (facts.has_escaping_jump) {
		errors.push_back(R"(The selection contains a "break" or "continue" whose loop is outside of the selection.)");
	}
	if (facts.has_super) {
		errors.push_back(R"(The selection calls "super", which can't be moved into another function.)");
	}

	///5. just declare the locals man
	auto is_declared_inside = [&](const LocalInfo& p_info) {
		return p_info.line >= sel_first1 && p_info.line <= sel_last1;
	};

	Vector<LocalInfo> params;
	for (const LocalInfo& u : facts.used) {
		if (!is_declared_inside(u)) {
			params.push_back(u);
		}
	}
	params.sort_custom<LocalLess>();

	HashSet<const void*> used_outside;
	{
		const bool in_loop = sel.in_loop;
		auto visit_outside = [&](const GP::Node* p_node, WalkState&) {
			if (p_node->type != GP::Node::IDENTIFIER) {
				return;
			}

			const GP::IdentifierNode* id = static_cast<const GP::IdentifierNode*>(p_node);
			LocalInfo li;
			if (!_local_from_identifier(id, ctx, li)) {
				return;
			}
			const bool outside = in_loop ? (id->start_line < sel_first1 || id->start_line > sel_last1) : (id->start_line > sel_last1);
			if (outside) {
				used_outside.insert(li.decl);
			}
		};

		WalkState state;
		_walk(fn->body, state, visit_outside);

	}

	Vector<LocalInfo> outputs;
	for (const LocalInfo& d : facts.declared) {
		if (used_outside.has(d.decl)) {
			outputs.push_back(d);
		}
	}

	for (const LocalInfo& u : facts.used) {
		if (!is_declared_inside(u) && facts.assigned.has(u.decl) && used_outside.has(u.decl)) {
			outputs.push_back(u);
		}
	}

	outputs.sort_custom<LocalLess>();

	if (outputs.size() > 1) {
		Vector<String> names;
		for (const LocalInfo& o : outputs) {
			names.push_back(String(o.name));
		}
		errors.push_back(vformat("The selection assigns several variables that are used afterwards (%s). Only one value can be returned, so select fewer statements.", String(", ").join(names)));
	}
	if (outputs.size() == 1 && outputs[0].is_constant) {
		errors.push_back(vformat(R"(The constant "%s" is declared in the selection but used after it. Move the declaration out first.)", String(outputs[0].name)));
	}

	if (!errors.is_empty()) {
		return bail();
	}

	/// ass. ass nirvana

	const String base_indent = _leading_whitespace(lines[sel_first1 - 1]);
	const String func_indent = _leading_whitespace(lines[fn->start_line - 1]);
	String unit = "\t";
	{
		const GP::Node* ass_first = fn->body->statements[0];
		if (ass_first != nullptr && ass_first->start_line > fn->start_line) {
			const String body_indent = _leading_whitespace(lines[ass_first->start_line - 1]);
			if (body_indent.length() > func_indent.length() && body_indent.begins_with(func_indent)) {
				unit = body_indent.substr(func_indent.length());
			}
		}
	}

	const String new_body_indent = func_indent + unit;

	Vector<String> param_decls;
	Vector<String> param_names;
	for (const LocalInfo& p : params) {
		param_names.push_back(String(p.name));
		param_decls.push_back(String(p.name) + (p.type_text.is_empty() ? String() : ": " + p.type_text));
	}

	String return_clause;
	if (outputs.is_empty()) {
		return_clause = " -> void";
	} else if (!outputs[0].type_text.is_empty()) {
		return_clause = " -> " + outputs[0].type_text;
	}

	const String header = func_indent + (fn->is_static ? "static " : "") + "func " + p_name + "(" + String(", ").join(param_decls) + ")" + return_clause + (_use_braces() ? String(" {") : String(":"));

	Vector<String> body_lines;
	for (int l = p_first_line; l <= sel_last1 - 1; l++) {
		const String& line = lines[l];
		if (facts.verbatim_lines.has(l)) {
			body_lines.push_back(line);
		} else if (line.strip_edges().is_empty()) {
			body_lines.push_back(String());
		} else if (line.begins_with(base_indent)) {
			body_lines.push_back(new_body_indent + line.substr(base_indent.length()));
		} else {
			body_lines.push_back(new_body_indent + line.lstrip(" \t"));
		}
	}

	if (outputs.size() == 1) {
		body_lines.push_back(new_body_indent + "return " + String(outputs[0].name));
	}

	String call = p_name + "(" + String(", ").join(param_names) + ")";
	if (facts.has_await) {
		call = "await " + call;
	}

	if (outputs.size() == 1) {
		const LocalInfo& o = outputs[0];
		if (o.declared_inside) {
			call = "var " + String(o.name) + (o.type_text.is_empty() ? String() : ": " + o.type_text) + " = " + call;
		} else {
			call = String(o.name) + " = " + call;
		}
	}
	const String call_line = base_indent + call;

	const int fn_last_line0 = fn->end_line - 1;
	if (fn_last_line0 < 0 || fn_last_line0 >= lines.size() || fn_last_line0 < p_first_line) {
		return fail("Could not find the end of the function... eh?!");
	}

	Edit insert_edit;
	insert_edit.start_line = fn_last_line0;
	insert_edit.start_column = lines[fn_last_line0].length();
	insert_edit.end_line = insert_edit.start_line;
	insert_edit.end_column = insert_edit.start_column;
	insert_edit.text = "\n\n\n" + header + "\n" + String("\n").join(body_lines);
	if (_use_braces()) {
		insert_edit.text += "\n" + func_indent + "}";
	}

	Edit replace_edit;
	replace_edit.start_line = p_first_line;
	replace_edit.start_column = 0;
	replace_edit.end_line = sel_last1 - 1;
	replace_edit.end_column = lines[sel_last1 - 1].length();
	replace_edit.text = call_line;

	res.edits.push_back(insert_edit);
	res.edits.push_back(replace_edit);
	res.signature = header.strip_edges();
	res.function_line = fn_last_line0 + 3 - (replace_edit.end_line - replace_edit.start_line);
	res.ok = true;
	return res;
}

Dictionary GDScriptRefactor::extract_function_dict(const String& p_source, const String& p_path, int p_first_line, int p_last_line, const String& p_name) {
	const ExtractResult r = extract_function(p_source, p_path, p_first_line, p_last_line, p_name);

	Dictionary d;
	d["ok"] = r.ok;
	d["error"] = r.error;
	d["signature"] = r.signature;
	d["function_line"] = r.function_line;

	Array edits;
	for (const Edit& e : r.edits) {
		Dictionary ed;
		ed["start_line"] = e.start_line;
		ed["start_column"] = e.start_column;
		ed["end_line"] = e.end_line;
		ed["end_column"] = e.end_column;
		ed["text"] = e.text;
		edits.push_back(ed);
	}
	d["edits"] = edits;
	return d;
}

#endif // TOOLS_ENABLED