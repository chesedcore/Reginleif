/**************************************************************************/
/*  attach_rules.cpp                                                      */
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

#include "attach_rules.h"

#include "core/object/script_language.h"
#include "scene/main/component.h"
#include "scene/main/composer.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h" // IWYU pragma: keep

bool AttachRules::validate(const Node* p_child, const Node* p_parent, String& r_error) {
	if (p_child == nullptr || p_parent == nullptr) {
		return true;
	}
	
	///wasn't as cancer as i thought it'd be to write, huh
	if (const Component* component = Object::cast_to<Component>(p_child)) {
		const Composer* composer = Object::cast_to<Composer>(p_parent);
		if (composer == nullptr) {
			r_error = vformat(R"(Component "%s" can only be a child of a Composer.)", component->get_name());
			return false;
		}
		return composer->can_accept(const_cast<Component*>(component), r_error);
	}

	if (const Composer* composer = Object::cast_to<Composer>(p_child)) {
		return composer->accepts_host(p_parent, r_error);
	}

	Ref<Script> script = p_child->get_script();
	if (script.is_null()) {
		return true;
	}
	StringName base;
	Vector<StringName> traits;

	Component::get_script_attach_constraints(script, base, traits);
	if (base == StringName() && traits.is_empty()) {
		return true;
	}
	return Composer::host_satisfies(p_parent, base, traits, vformat(R"(Node "%s")", p_child->get_name()), r_error);
}

void AttachRules::get_node_errors(const Node* p_node, Vector<String>& r_errors) {
	ERR_FAIL_NULL(p_node);
	if (const Composer* composer = Object::cast_to<Composer>(p_node)) {
		composer->get_contract_errors(r_errors);
		return;
	}
	const Node* parent = p_node->get_parent();
	String err;
	if (parent != nullptr && !validate(p_node, parent, err)) {
		r_errors.push_back(err);
	}

	if (const Component* component = Object::cast_to<Component>(p_node)) {
		if (const Composer* composer = Object::cast_to<Composer>(parent)) {
			composer->get_dependency_errors(component, r_errors);
		}
	}
}

void AttachRules::collect_scene_errors(const Node* p_root, Vector<String>& r_errors) {
	ERR_FAIL_NULL(p_root);
	Vector<String> mine;

	get_node_errors(p_root, mine);
	for (const String& e : mine) {
		r_errors.push_back(vformat("%s: %s", String(p_root->get_name()), e));
	}
	for (int i = 0; i < p_root->get_child_count(false); i++) {
		collect_scene_errors(p_root->get_child(i, false), r_errors);
	}
}

static void _revalidate_subtree(Node* p_node) {
	const bool managed = Object::cast_to<Composer>(p_node) != nullptr || Object::cast_to<Component>(p_node) != nullptr;
	if (p_node->get_script().get_type() != Variant::NIL || managed) {
		if (!managed) {

			Vector<String> errors;
			AttachRules::get_node_errors(p_node, errors);
			for (const String& e : errors) {
				Composer::fail_contract(e);
			}

		}
		p_node->update_configuration_warnings();

	}
	for (int i = 0; i < p_node->get_child_count(true); i++) {
		_revalidate_subtree(p_node->get_child(i, true));
	}
}

void AttachRules::revalidate_all() {
	Composer::revalidate_all();
	SceneTree* tree = SceneTree::get_singleton();
	if (tree == nullptr) {
		return;
	}
	Node* root = tree->get_root();
	if (root != nullptr) {
		_revalidate_subtree(root);
	}
}
