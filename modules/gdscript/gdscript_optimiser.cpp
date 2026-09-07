/**************************************************************************/
/*  gdscript_optimiser.cpp                                                */
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

#include "gdscript_optimiser.h"

HashMap<const GDScriptParser::Node*, GDScriptOptimiser::VarLifetime> GDScriptOptimiser::compute_lifetimes(const GDScriptParser::SuiteNode* p_block) {
	HashMap<const GDScriptParser::Node*, VarLifetime> lifetimes;
	if (p_block == nullptr) {
		return lifetimes;
	}
	for (const GDScriptParser::Node* stmt : p_block->statements) {
		_visit(stmt, lifetimes);
	}
	return lifetimes;
}

void GDScriptOptimiser::_visit_identifier(const GDScriptParser::IdentifierNode* p_id, HashMap<const GDScriptParser::Node*, VarLifetime>& r_last_use, bool p_is_write) {
	if (p_id == nullptr) {
		return;
	}

	const GDScriptParser::Node* key = nullptr;
	if (p_id->source == GDScriptParser::IdentifierNode::LOCAL_VARIABLE && p_id->variable_source != nullptr) {
		key = p_id->variable_source;
	} else if (p_id->source == GDScriptParser::IdentifierNode::LOCAL_BIND && p_id->bind_source != nullptr) {
		key = p_id->bind_source;
	} else {
		return;
	}

	VarLifetime& lt = r_last_use[key];
	if (p_is_write) {
		if (p_id->start_line > lt.last_write) {
			lt.last_write = p_id->start_line;
		}
	} else {
		if (p_id->start_line > lt.last_read) {
			lt.last_read = p_id->start_line;
		}
	}
}

///helper that reaches into EVERY SINGLE FUCKING PARSED NODE TYPE ever 
void GDScriptOptimiser::_visit(const GDScriptParser::Node* p_node, HashMap<const GDScriptParser::Node*, VarLifetime>& r_last_use) {
	if (p_node == nullptr) {
		return;
	}

	switch (p_node->type) {
		case GDScriptParser::Node::IDENTIFIER: {
			_visit_identifier(static_cast<const GDScriptParser::IdentifierNode*>(p_node), r_last_use, false);
		} break;
		case GDScriptParser::Node::ASSIGNMENT: {
            ///yep, i've got the free will to ignore the fucking 'never auto' rule now
            ///took me all this time to finally break free from upstream hell
			const auto* n = static_cast<const GDScriptParser::AssignmentNode*>(p_node);
			if (n->assignee->type == GDScriptParser::Node::IDENTIFIER) {
				_visit_identifier(static_cast<const GDScriptParser::IdentifierNode*>(n->assignee), r_last_use, true);
			}
			_visit(n->assigned_value, r_last_use);
		} break;
		case GDScriptParser::Node::AWAIT: {
			_visit(static_cast<const GDScriptParser::AwaitNode*>(p_node)->to_await, r_last_use);
		} break;
		case GDScriptParser::Node::BINARY_OPERATOR: {
			const auto* n = static_cast<const GDScriptParser::BinaryOpNode*>(p_node);
			_visit(n->left_operand, r_last_use);
			_visit(n->right_operand, r_last_use);
		} break;
		case GDScriptParser::Node::UNARY_OPERATOR: {
			_visit(static_cast<const GDScriptParser::UnaryOpNode*>(p_node)->operand, r_last_use);
		} break;
		case GDScriptParser::Node::TERNARY_OPERATOR: {
			const auto* n = static_cast<const GDScriptParser::TernaryOpNode*>(p_node);
			_visit(n->condition, r_last_use);
			_visit(n->true_expr, r_last_use);
			_visit(n->false_expr, r_last_use);
		} break;
		case GDScriptParser::Node::CALL: {
			const auto* n = static_cast<const GDScriptParser::CallNode*>(p_node);
			_visit(n->callee, r_last_use);
			for (const GDScriptParser::ExpressionNode* arg : n->arguments) {
				_visit(arg, r_last_use);
			}
		} break;
		case GDScriptParser::Node::CAST: {
			_visit(static_cast<const GDScriptParser::CastNode*>(p_node)->operand, r_last_use);
		} break;
		case GDScriptParser::Node::SUBSCRIPT: {
			const auto* n = static_cast<const GDScriptParser::SubscriptNode*>(p_node);
			_visit(n->base, r_last_use);
			if (!n->is_attribute) {
				_visit(n->index, r_last_use);
			}
		} break;
		case GDScriptParser::Node::ARRAY: {
			for (const GDScriptParser::ExpressionNode* elem : static_cast<const GDScriptParser::ArrayNode*>(p_node)->elements) {
				_visit(elem, r_last_use);
			}
		} break;
		case GDScriptParser::Node::DICTIONARY: {
			for (const GDScriptParser::DictionaryNode::Pair& pair : static_cast<const GDScriptParser::DictionaryNode*>(p_node)->elements) {
				_visit(pair.key, r_last_use);
				_visit(pair.value, r_last_use);
			}
		} break;
		case GDScriptParser::Node::PRELOAD: {
			_visit(static_cast<const GDScriptParser::PreloadNode*>(p_node)->path, r_last_use);
		} break;
		case GDScriptParser::Node::ASSERT: {
			const auto* n = static_cast<const GDScriptParser::AssertNode*>(p_node);
			_visit(n->condition, r_last_use);
			_visit(n->message, r_last_use);
		} break;
		case GDScriptParser::Node::RETURN: {
			_visit(static_cast<const GDScriptParser::ReturnNode*>(p_node)->return_value, r_last_use);
		} break;
		case GDScriptParser::Node::VARIABLE: {
			_visit(static_cast<const GDScriptParser::VariableNode*>(p_node)->initializer, r_last_use);
		} break;
		case GDScriptParser::Node::LAMBDA: {
			for (const GDScriptParser::IdentifierNode* cap : static_cast<const GDScriptParser::LambdaNode*>(p_node)->captures) {
				_visit_identifier(cap, r_last_use, false);
			}
		} break;
		case GDScriptParser::Node::TYPE_TEST: {
			_visit(static_cast<const GDScriptParser::TypeTestNode*>(p_node)->operand, r_last_use);
		} break;
		case GDScriptParser::Node::IF: {
			const auto* n = static_cast<const GDScriptParser::IfNode*>(p_node);
			_visit(n->condition, r_last_use);
			if (n->true_block != nullptr) {
				for (const GDScriptParser::Node* s : n->true_block->statements) {
					_visit(s, r_last_use);
				}
			}
			if (n->false_block != nullptr) {
				for (const GDScriptParser::Node* s : n->false_block->statements) {
					_visit(s, r_last_use);
				}
			}
		} break;
		case GDScriptParser::Node::FOR: {
			const auto* n = static_cast<const GDScriptParser::ForNode*>(p_node);
			_visit(n->list, r_last_use);
			if (n->loop != nullptr) {
				for (const GDScriptParser::Node* s : n->loop->statements) {
					_visit(s, r_last_use);
				}
			}
		} break;
		case GDScriptParser::Node::WHILE: {
			const auto* n = static_cast<const GDScriptParser::WhileNode*>(p_node);
			_visit(n->condition, r_last_use);
			if (n->loop != nullptr) {
				for (const GDScriptParser::Node* s : n->loop->statements) {
					_visit(s, r_last_use);
				}
			}
		} break;
		case GDScriptParser::Node::MATCH: {
			const auto* n = static_cast<const GDScriptParser::MatchNode*>(p_node);
			_visit(n->test, r_last_use);
			for (const GDScriptParser::MatchBranchNode* branch : n->branches) {
				if (branch->block != nullptr) {
					for (const GDScriptParser::Node* s : branch->block->statements) {
						_visit(s, r_last_use);
					}
				}
				if (branch->guard_body != nullptr) {
					for (const GDScriptParser::Node* s : branch->guard_body->statements) {
						_visit(s, r_last_use);
					}
				}
				for (const GDScriptParser::PatternNode* pat : branch->patterns) {
					_visit_pattern(pat, r_last_use);
				}
			}
		} break;
		default:
			///everything else either can't reference, or is irrelevant here, probs
			break;
	}
}

///recurse fully so nested reads can actually give a shit about last use tracking
void GDScriptOptimiser::_visit_pattern(const GDScriptParser::PatternNode* p_pattern, HashMap<const GDScriptParser::Node*, VarLifetime>& r_last_use) {
	if (p_pattern == nullptr) {
		return;
	}

	switch (p_pattern->pattern_type) {
		case GDScriptParser::PatternNode::PT_EXPRESSION: {
			_visit(p_pattern->expression, r_last_use);
		} break;
		case GDScriptParser::PatternNode::PT_ARRAY: {
			for (const GDScriptParser::PatternNode* sub : p_pattern->array) {
				_visit_pattern(sub, r_last_use);
			}
		} break;
		case GDScriptParser::PatternNode::PT_DICTIONARY: {
			for (const GDScriptParser::PatternNode::Pair& pair : p_pattern->dictionary) {
				_visit(pair.key, r_last_use);
				if (pair.value_pattern != nullptr) {
					_visit_pattern(pair.value_pattern, r_last_use);
				}
			}
		} break;
		case GDScriptParser::PatternNode::PT_BIND: {
			if (p_pattern->bind != nullptr) {
				VarLifetime& lt = r_last_use[p_pattern->bind];
				if (p_pattern->bind->start_line > lt.last_write) {
					lt.last_write = p_pattern->bind->start_line;
				}
			}
		} break;
		case GDScriptParser::PatternNode::PT_LITERAL:
		case GDScriptParser::PatternNode::PT_REST:
		case GDScriptParser::PatternNode::PT_WILDCARD:
			break;
	}
}