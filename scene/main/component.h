/**************************************************************************/
/*  component.h                                                           */
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

#include "scene/main/node.h"

class Composer;
class Script;

class Component : public Node {
	GDCLASS(Component, Node);

	friend class Composer;

	Node* host = nullptr;

	void _set_host(Node* p_host) { host = p_host; }

protected:
	static void _bind_methods();
	void _notification(int p_what);

	GDVIRTUAL0(_on_attach)
	GDVIRTUAL1(_physics_tick, double)
	GDVIRTUAL1(_tick, double)
	GDVIRTUAL0(_on_detach)

public:
	Node* get_host() const { return host; }
	bool has_physics_tick() const;
	bool has_tick() const;

	void _call_on_attach();
	void _call_on_detach();
	void _call_physics_tick(double p_delta);
	void _call_tick(double p_delta);

	typedef void (*AttachConstraintsFn)(const Ref<Script>& p_script, StringName& r_base, Vector<StringName>& r_traits);
	static AttachConstraintsFn attach_constraints_provider;
	static void get_script_attach_constraints(const Ref<Script>& p_script, StringName& r_base, Vector<StringName>& r_traits);

	struct SiblingDep {
		StringName member;
		StringName type;
		bool required = false;
	};
	typedef void (*SiblingDepsFn)(const Ref<Script>& p_script, Vector<SiblingDep>& r_deps);
	static SiblingDepsFn sibling_deps_provider;
	void get_sibling_deps(Vector<SiblingDep>& r_deps) const;

	void get_attach_constraints(StringName& r_base, Vector<StringName>& r_traits) const;

};
