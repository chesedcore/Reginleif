/**************************************************************************/
/*  test_composer.cpp                                                     */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_composer)

#include "scene/2d/node_2d.h"
#include "scene/main/component.h"
#include "scene/main/composer.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"

namespace TestComposer {

struct Rig {
	Node2D* host = nullptr;
	Composer* composer = nullptr;
	Component* component = nullptr;

	Rig(bool p_with_component = true) {
		host = memnew(Node2D);
		composer = memnew(Composer);
		SceneTree::get_singleton()->get_root()->add_child(host);
		host->add_child(composer);
		if (p_with_component) {
			component = memnew(Component);
			composer->add_child(component);
		}
	}

	~Rig() {
		SceneTree::get_singleton()->get_root()->remove_child(host);
		memdelete(host);
	}
};

TEST_CASE("[SceneTree][Composer] class_is_a walks native chains") {
	CHECK(Composer::class_is_a("Node2D", "Node2D"));
	CHECK(Composer::class_is_a("Node2D", "CanvasItem"));
	CHECK(Composer::class_is_a("Node2D", "Node"));
	CHECK_FALSE(Composer::class_is_a("Node", "Node2D"));
	CHECK_FALSE(Composer::class_is_a("Node2D", "Control"));
	CHECK_FALSE(Composer::class_is_a("DefinitelyNotARealClass", "Node"));
	CHECK_FALSE(Composer::class_is_a(StringName(), "Node"));
}

TEST_CASE("[SceneTree][Composer] host_satisfies checks the host class") {
	Node2D* n2d = memnew(Node2D);
	Node* plain = memnew(Node);
	Vector<StringName> no_traits;
	String err;

	CHECK(Composer::host_satisfies(n2d, StringName(), no_traits, "test", err));
	CHECK(Composer::host_satisfies(n2d, "Node2D", no_traits, "test", err));
	CHECK(Composer::host_satisfies(n2d, "CanvasItem", no_traits, "test", err));

	err = String();
	CHECK_FALSE(Composer::host_satisfies(plain, "Node2D", no_traits, "test", err));
	CHECK_FALSE(err.is_empty());

	memdelete(n2d);
	memdelete(plain);
}

TEST_CASE("[SceneTree][Composer] host_satisfies traits go through trait_check") {
	Node2D* n2d = memnew(Node2D);
	Vector<StringName> traits;
	traits.push_back("Flying");
	String err;

	Composer::TraitCheckFn saved = Composer::trait_check;

	Composer::trait_check = nullptr;
	CHECK_FALSE(Composer::host_satisfies(n2d, StringName(), traits, "test", err));

	Composer::trait_check = [](const Node*, const StringName& p_trait) { return p_trait == StringName("Flying"); };
	CHECK(Composer::host_satisfies(n2d, StringName(), traits, "test", err));

	traits.push_back("Swimming");
	CHECK_FALSE(Composer::host_satisfies(n2d, StringName(), traits, "test", err));

	Composer::trait_check = saved;
	memdelete(n2d);
}

TEST_CASE("[SceneTree][Composer] host is wired up on parent and cleared on unparent") {
	Rig rig;
	CHECK(rig.composer->get_host() == rig.host);
	CHECK(rig.component->get_host() == rig.host);

	rig.host->remove_child(rig.composer);
	CHECK(rig.composer->get_host() == nullptr);
	CHECK(rig.component->get_host() == nullptr);

	rig.host->add_child(rig.composer);
	CHECK(rig.composer->get_host() == rig.host);
}

TEST_CASE("[SceneTree][Composer] component leaving the composer loses its host") {
	Rig rig;
	CHECK(rig.component->get_host() == rig.host);

	rig.composer->remove_child(rig.component);
	CHECK(rig.component->get_host() == nullptr);

	memdelete(rig.component);
	rig.component = nullptr;
}

TEST_CASE("[SceneTree][Composer] find_composer and get_component") {
	Rig rig;
	CHECK(Composer::find_composer(rig.host) == rig.composer);
	CHECK(rig.composer->get_component("Component") == rig.component);
	CHECK(rig.composer->get_component("Node2D") == nullptr);

	Node2D* bare = memnew(Node2D);
	CHECK(Composer::find_composer(bare) == nullptr);
	memdelete(bare);
}

TEST_CASE("[SceneTree][Composer] unconstrained components are always accepted") {
	Rig rig(false);
	Component* extra = memnew(Component);
	String err;
	CHECK(rig.composer->can_accept(extra, err));
	CHECK(Composer::host_accepts(rig.host, extra, err));
	memdelete(extra);
}

TEST_CASE("[SceneTree][Composer] composer's own extra_base constrains its host") {
	Rig rig(false);
	String err;

	rig.composer->set_extra_base("Node2D");
	CHECK(rig.composer->accepts_host(rig.host, err));

	rig.composer->set_extra_base("Control");
	err = String();
	CHECK_FALSE(rig.composer->accepts_host(rig.host, err));
	CHECK_FALSE(err.is_empty());

	rig.composer->set_extra_base(StringName());
	CHECK(rig.composer->accepts_host(rig.host, err));
}

TEST_CASE("[SceneTree][Composer] aggregate_constraints with bumfuck nothing to aggregate is empty") {
	Rig rig(false);
	StringName base;
	Vector<StringName> traits;
	String conflict;
	rig.composer->aggregate_constraints(nullptr, base, traits, conflict);
	CHECK(base == StringName());
	CHECK(traits.is_empty());
	CHECK(conflict.is_empty());
}

TEST_CASE("[SceneTree][Composer] get_contract_errors is A-OK for a good rig") {
	Rig rig;
	Vector<String> errors;
	rig.composer->get_contract_errors(errors);
	CHECK(errors.is_empty());
}

TEST_CASE("[SceneTree][Composer] warnings flag noncomponent children") {
	Rig rig(false);
	Node* intruder = memnew(Node);
	rig.composer->add_child(intruder);
	CHECK(rig.composer->get_configuration_warnings().size() > 0);
}

} // namespace TestComposer