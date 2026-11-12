/**************************************************************************/
/*  program_index_summary.cpp                                             */
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


///!!RULES!!
///parsing is OK, but you may NOT analyse!!
///do not ever touch GDScriptCache's mutex either!!
///this is done to ensure ProgramIndexService::ensure_populated() is safe to call!

///p.s. listen to Outlier from Ace Combat 8 while reading this file, that's all thanks :>

#include "program_index.h"
#include "program_index_gd.h"

#include "gdscript.h"
#include "gdscript_cache.h"
#include "gdscript_parser.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"

namespace RGIndex {

String canonical_path(const String& p_path) {
	String s = GDScript::canonicalize_path(p_path);
	if (s.begins_with("res://")) {
		return s;
	}
	s = ProjectSettings::get_singleton()->localize_path(s);
	return s.simplify_path();
}

String TypeKey::debug_string() const {
	switch (kind) {
		case NONE:
			return "<none>";
		case BUILTIN:
			return vformat("builtin(%s)", Variant::get_type_name((Variant::Type)id));
		case NATIVE:
			return "native(" + String(name) + ")";
		case SCRIPT_CLASS:
			return "class(" + String(name) + ")";
		case ENUM:
			return "enum(" + String(name) + ")";
	}
	return String();
}

uint64_t TypeRef::hash() const {
	uint64_t h = hash_combine((uint64_t)kind, (uint64_t)builtin);
	h = hash_combine(h, name.hash());
	for (const StringName& s : chain_tail) {
		h = hash_combine(h, s.hash());
	}
	for (const TypeRef& a : args) {
		h = hash_combine(h, a.hash());
	}
	if (callable_signature) {
		h = hash_combine(h, 0xca11ab1eULL);
	}
	for (const StringName& s : trait_bounds) {
		h = hash_combine(h, s.hash());
	}
	return hash_combine(h, resolved.hash64());
}

String TypeRef::debug_string() const {
	switch (kind) {
		case VARIANT:
			return "Variant";
		case VOID_TYPE:
			return "void";
		case BUILTIN:
			return Variant::get_type_name(builtin);
		case GENERIC_PARAM:
			return "$" + String(name);
		case TRAIT_BOUND: {
			String s = "impl ";
			for (int i = 0; i < trait_bounds.size(); i++) {
				s += (i ? " + " : "") + String(trait_bounds[i]);
			}
			return s;
		}
		case NAMED: {
			String s = name;
			for (const StringName& t : chain_tail) {
				s += "." + String(t);
			}
			return s;
		}
	}
	return String();
}

uint64_t compute_sig_hash(const MethodSig& p_sig) {

	uint64_t h = hash_combine((uint64_t)p_sig.default_arg_count, (p_sig.is_vararg ? 2 : 0) | (p_sig.is_static ? 1 : 0));
	for (const TypeRef& t : p_sig.param_types) {
		h = hash_combine(h, t.hash());
	}
	return hash_combine(h, p_sig.return_type.hash());
}

uint32_t Query::hash() const {
	uint32_t h = hash_murmur3_one_32((uint32_t)kind, type.hash());
	h = hash_murmur3_one_32(a.hash(), h);
	return hash_murmur3_one_32(b.hash(), h);
}

namespace {

///im tired boss.
using GP = GDScriptParser;

struct SourceLines {
	const String& source;
	Vector<int> starts; ///starts[i] is the char offset of line i+1

	explicit SourceLines(const String& p_source) :
	source(p_source) {
		starts.push_back(0);
		for (int i = 0; i < source.length(); i++) {
			if (source[i] == '\n') {
				starts.push_back(i + 1);
			}
		}
	}

	uint64_t hash_lines(int p_first, int p_last) const {
		if (source.is_empty() || p_first < 1 || p_last < p_first || p_first > starts.size()) {
			return 0; ///.gdc has no body
		}
		const int a = starts[p_first - 1];
		const int b = p_last < starts.size() ? starts[p_last] : source.length();
		return source.substr(a, b - a).hash64();
	}

	Vector<String> signature_defaults(int p_func_line, const StringName& p_name, int p_param_count) const {
		Vector<String> defaults;
		defaults.resize(p_param_count);

		const int n = source.length();
		if (n == 0 || p_func_line < 1 || p_func_line > starts.size()) {
			return defaults;
		}

		const String name = String(p_name);
		auto is_ident = [](char32_t c) {
			return c == '_' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c > 127;
		};

		int i = starts[p_func_line - 1];
		bool found = false;
		while (i < n) {
			const int at = source.find("func", i);
			if (at < 0) {
				return defaults;
			}
			i = at + 4;
			if ((at > 0 && is_ident(source[at - 1])) || i >= n || (source[i] != ' ' && source[i] != '\t')) {
				continue;
			}
			int k = i;
			while (k < n && (source[k] == ' ' || source[k] == '\t')) {
				k++;
			}
			if (source.substr(k, name.length()) == name && (k + name.length() >= n || !is_ident(source[k + name.length()]))) {
				i = k + name.length();
				found = true;
				break;
			}
		}
		if (!found) {
			return defaults;
		}

		while (i < n && source[i] != '(') {
			i++;
		}
		if (i >= n) {
			return defaults;
		}
		i++;

		int depth = 0;
		int param = 0;
		int def_start = -1;
		auto finish = [&](int p_end) {
			if (param < p_param_count && def_start >= 0) {
				String text = source.substr(def_start, p_end - def_start).replace("\n", " ").replace("\t", " ").strip_edges();
				if (text.length() > 40) {
					text = text.substr(0, 37) + String(U"\u2026");
				}
				defaults.write[param] = text;
			}
		};

		for (; i < n; i++) {
			const char32_t c = source[i];

			if (c == '#') {
				while (i < n && source[i] != '\n') {
					i++;
				}
				continue;
			}

			if (c == '"' || c == '\'') {
				const bool triple = i + 2 < n && source[i + 1] == c && source[i + 2] == c;
				if (triple) {
					i += 3;
					while (i + 2 < n && !(source[i] == c && source[i + 1] == c && source[i + 2] == c)) {
						if (source[i] == '\\') {
							i++;
						}
						i++;
					}
					i += 2;
				} else {
					i++;
					while (i < n && source[i] != c && source[i] != '\n') {
						if (source[i] == '\\') {
							i++;
						}
						i++;
					}
				}
				continue;
			}

			if (c == '(' || c == '[' || c == '{') {
				depth++;
			} else if (c == ')' || c == ']' || c == '}') {
				if (depth == 0) {
					finish(i); ///the closing paren of the parameter list
					return defaults;
				}
				depth--;
			} else if (c == ',' && depth == 0) {
				finish(i);
				param++;
				def_start = -1;
			} else if (c == '=' && depth == 0 && def_start < 0) {
				if (i + 1 >= n || source[i + 1] != '=') {
					def_start = i + 1;
				}
			}
		}
		return defaults;
	}
};

TypeRef _make_type_ref(const GP::TypeNode* p_type, const HashSet<StringName>& p_generics) {
	TypeRef r;
	if (p_type == nullptr) {
		r.kind = TypeRef::VARIANT; ///no anno
		return r;
	}

	if (!p_type->trait_bounds.is_empty()) {
		r.kind = TypeRef::TRAIT_BOUND;
		for (const GP::IdentifierNode* id : p_type->trait_bounds) {
			if (id != nullptr) {
				r.trait_bounds.push_back(id->name);
			}
		}
		return r;
	}

	if (p_type->type_chain.is_empty()) {
		r.kind = TypeRef::VOID_TYPE;
		return r;
	}

	for (uint32_t i = 0; i < p_type->container_types.size(); i++) {
		r.args.push_back(_make_type_ref(p_type->container_types[i], p_generics));
	}

	if (p_type->is_callable_signature && p_type->type_chain.size() == 1 && p_type->type_chain[0]->name == SNAME("Callable")) {
		r.callable_signature = true;
		if (p_type->callable_return != nullptr) {
			r.args.push_back(_make_type_ref(p_type->callable_return, p_generics));
		} else {
			TypeRef void_ref;
			void_ref.kind = TypeRef::VOID_TYPE;
			r.args.push_back(void_ref);
		}
	}

	const StringName first = p_type->type_chain[0]->name;

	if (p_type->type_chain.size() == 1) {

		if (p_generics.has(first)) {
			r.kind = TypeRef::GENERIC_PARAM;
			r.name = first;
			return r;
		}

		if (first == "Variant") {
			r.kind = TypeRef::VARIANT;
			r.args.clear();
			return r;
		}

		const Variant::Type vt = Variant::get_type_by_name(first);

		if (vt < Variant::VARIANT_MAX && vt != Variant::OBJECT && vt != Variant::NIL) {
			r.kind = TypeRef::BUILTIN;
			r.builtin = vt;
			return r;
		}
	}

	r.kind = TypeRef::NAMED;
	r.name = first;

	for (uint32_t i = 1; i < p_type->type_chain.size(); i++) {
		r.chain_tail.push_back(p_type->type_chain[i]->name);
	}
	return r;
}

MethodDecl _make_method(const GP::FunctionNode* p_fn, const HashSet<StringName>& p_class_generics, const SourceLines& p_lines) {
	MethodDecl m;
	m.name = p_fn->identifier->name;

	HashSet<StringName> generics(p_class_generics);
	const Vector<String> verbatim_defaults = p_lines.signature_defaults(p_fn->start_line, p_fn->identifier->name, (int)p_fn->parameters.size());
	for (const GP::IdentifierNode* g : p_fn->generic_parameters) {
		if (g != nullptr) {
			generics.insert(g->name);
		}
	}

	for (uint32_t i = 0; i < p_fn->parameters.size(); i++) {
		const GP::ParameterNode* p = p_fn->parameters[i];
		m.sig.param_types.push_back(_make_type_ref(p->datatype_specifier, generics));
		m.sig.param_names.push_back(p->identifier != nullptr ? p->identifier->name : StringName());
		String default_text;
		if (p->initializer != nullptr) {
			default_text = i < (uint32_t)verbatim_defaults.size() && !verbatim_defaults[i].is_empty() ? verbatim_defaults[i] : String(U"\u2026");
		}
		m.sig.param_defaults.push_back(default_text);
		if (p->initializer != nullptr) {
			m.sig.default_arg_count++;
		}
	}
	m.sig.return_type = _make_type_ref(p_fn->return_type, generics);
	m.sig.is_vararg = p_fn->is_vararg();
	m.sig.is_static = p_fn->is_static;
	m.sig.sig_hash = compute_sig_hash(m.sig);

	m.has_body = p_fn->has_explicit_body;
	m.start_line = p_fn->start_line;
#ifdef TOOLS_ENABLED
	m.doc = p_fn->doc_data.description;
#endif // TOOLS_ENABLED
	m.body_hash = p_lines.hash_lines(p_fn->start_line, p_fn->end_line);
	return m;
}

ImplDecl _make_impl(const GP::ImplNode* p_impl, const StringName& p_default_trait, const StringName& p_scope_class_key, const HashSet<StringName>& p_generics, const SourceLines& p_lines) {
	ImplDecl d;

	d.trait_name = p_impl->trait_name != nullptr ? p_impl->trait_name->name : p_default_trait;
	d.explicit_target = p_impl->impl_target_type != nullptr;

	if (d.explicit_target) {
		d.target = _make_type_ref(p_impl->impl_target_type, p_generics);
	}

	d.scope_class_key = p_scope_class_key;
	d.line = p_impl->start_line;

	for (const GP::FunctionNode* fn : p_impl->methods) {
		if (fn != nullptr && fn->identifier != nullptr) {
			d.provided.push_back(_make_method(fn, p_generics, p_lines));
		}
	}

	return d;
}

void _collect_class(const GP::ClassNode* p_class, const StringName& p_outer_key, const String& p_canon_path, const SourceLines& p_lines, FileSummary& r_sum) {
	const bool is_root = p_outer_key == StringName();

	if (!is_root && p_class->identifier == nullptr) {
		return; ///inner class bork
	}

	ClassDecl c;
	c.outer_key = p_outer_key;
	c.key = is_root ? StringName(p_canon_path) : StringName(String(p_outer_key) + "::" + String(p_class->identifier->name));
	c.parser_fqcn = StringName(p_class->fqcn);
	c.local_name = p_class->identifier != nullptr ? p_class->identifier->name : StringName();
	c.global_name = is_root ? c.local_name : StringName();
	c.is_abstract = p_class->is_abstract;

	HashSet<StringName> generics;
	for (const GP::IdentifierNode *g : p_class->generic_parameters) {
		if (g != nullptr) {
			c.generic_params.push_back(g->name);
			generics.insert(g->name);
		}
	}

	
	if (!p_class->extends_used) {
		c.base.kind = BaseRef::IMPLICIT;
	} else if (!p_class->extends_path.is_empty()) {

		String path = p_class->extends_path;
		if (path.is_relative_path()) {
			path = p_canon_path.get_base_dir().path_join(path);
		}
		c.base.kind = BaseRef::PATH;
		c.base.path = canonical_path(path.simplify_path());

		for (const GP::IdentifierNode* id : p_class->extends) {
			c.base.chain_tail.push_back(id->name);
		}

	} else if (p_class->extends.size() >= 1) {
		c.base.kind = BaseRef::NAME;
		c.base.name = p_class->extends[0]->name;

		for (int i = 1; i < p_class->extends.size(); i++) {
			c.base.chain_tail.push_back(p_class->extends[i]->name);
		}

	}
	for (const GP::TypeNode* arg : p_class->extends_generic_args) {
		c.base.generic_args.push_back(_make_type_ref(arg, generics));
	}
	for (const GP::TypeNode* arg : p_class->attach_to_types) {
		c.attach_to.push_back(_make_type_ref(arg, generics));
	}

	for (uint32_t i = 0; i < p_class->members.size(); i++) {
		const GP::ClassNode::Member& mem = p_class->members[i];
		if (mem.type == GP::ClassNode::Member::FUNCTION && mem.function != nullptr && mem.function->identifier != nullptr) {
			c.methods.push_back(_make_method(mem.function, generics, p_lines));
		} else if (mem.type == GP::ClassNode::Member::ENUM && mem.m_enum != nullptr && mem.m_enum->identifier != nullptr) {
			c.enum_names.insert(mem.m_enum->identifier->name);
		}
	}

	const StringName key = c.key;
	r_sum.classes.push_back(c); ///preorder

	for (const GP::ImplNode* impl : p_class->impls) {
		if (impl != nullptr) {
			r_sum.impls.push_back(_make_impl(impl, StringName(), key, generics, p_lines));
		}
	}

	for (uint32_t i = 0; i < p_class->members.size(); i++) {
		const GP::ClassNode::Member& mem = p_class->members[i];
		if (mem.type == GP::ClassNode::Member::CLASS && mem.m_class != nullptr) {
			_collect_class(mem.m_class, key, p_canon_path, p_lines, r_sum);
		}
	}
}

}///namespace

///filesummary builders from here on out

Ref<FileSummary> FileSummary::build_from_parser(const GDScriptParser& p_parser, const String& p_path, const String& p_source) {

	Ref<FileSummary> s;
	s.instantiate();
	s->path = canonical_path(p_path);
	s->content_hash = p_source.hash64();
	s->is_tool = p_parser.is_tool();
	const SourceLines lines(p_source);

	if (p_parser.is_trait_script()) {

		s->is_trait_file = true;
		const GP::TraitNode* t = p_parser.get_trait_tree();

		if (t != nullptr && t->identifier != nullptr) {
			TraitDecl td;
			td.name = t->identifier->name;
			HashSet<StringName> generics;

			for (const GP::IdentifierNode* g : t->generic_parameters) {
				if (g != nullptr) {
					td.generic_params.push_back(g->name);
					generics.insert(g->name);
				}
			}

			for (const GP::FunctionNode* fn : t->methods) {
				if (fn != nullptr && fn->identifier != nullptr) {
					td.required.push_back(_make_method(fn, generics, lines));
				}
			}

			for (const GP::FunctionNode* fn : t->default_methods) {
				if (fn != nullptr && fn->identifier != nullptr) {
					td.defaults.push_back(_make_method(fn, generics, lines));
				}
			}

			for (const GP::ImplNode* impl : t->impls) {
				if (impl != nullptr) {
					s->impls.push_back(_make_impl(impl, td.name, StringName(), generics, lines));
				}
			}

			s->traits.push_back(td);
		}
		return s;
	}

	const GP::ClassNode* root = p_parser.get_tree();
	if (root == nullptr) {
		s->parse_failed = true;
		return s;
	}

	_collect_class(root, StringName(), s->path, lines, *s.ptr());
	return s;
}

Ref<FileSummary> FileSummary::build_from_source(const String& p_path, const String& p_source) {
	GDScriptParser parser;
	const Error err = parser.parse(p_source, p_path, false, /*p_parse_body=*/true);
	Ref<FileSummary> s = build_from_parser(parser, p_path, p_source);
	s->parse_failed = s->parse_failed || err != OK;
	return s;
}

Ref<FileSummary> FileSummary::build_from_file(const String& p_path) {
	const String remapped = ResourceLoader::path_remap(p_path);
	if (!FileAccess::exists(remapped)) {
		return Ref<FileSummary>();
	}

	if (remapped.has_extension("gdc")) {
		const Vector<uint8_t> tokens = GDScriptCache::get_binary_tokens(remapped);
		if (tokens.is_empty()) {
			return Ref<FileSummary>();
		}
		GDScriptParser parser;
		const Error err = parser.parse_binary(tokens, p_path);
		Ref<FileSummary> s = build_from_parser(parser, p_path, String());
		s->content_hash = hash_djb2_buffer(tokens.ptr(), tokens.size());
		s->parse_failed = s->parse_failed || err != OK;
		return s;
	}

	const String source = GDScriptCache::get_source_code(remapped);
	if (source.is_empty()) {
		return Ref<FileSummary>();
	}
	return build_from_source(p_path, source);
}

TypeKey type_key_from_datatype(const GDScriptParser::DataType& p_type, const String& p_fallback_script_path) {
	using DT = GDScriptParser::DataType;

	///insert obligatory joke about bashing godot upstream for not allowing auto where it matters
	auto class_key = [&](const GP::ClassNode* p_class, const String& p_script_path) -> String {

		if (p_class == nullptr) {
			return String();
		}

		Vector<StringName> chain;

		for (const GP::ClassNode*c = p_class; c != nullptr && c->outer != nullptr; c = c->outer) {
			if (c->identifier == nullptr) {
				return String();
			}
			chain.push_back(c->identifier->name);
		}

		const String path = p_script_path.is_empty() ? p_fallback_script_path : p_script_path;
		if (path.is_empty()) {
			return String();
		}

		String key = canonical_path(path);
		for (int i = chain.size() - 1; i >= 0; i--) {
			key += "::" + String(chain[i]);
		}

		return key;
	};

	switch (p_type.kind) {
		case DT::BUILTIN:
			return TypeKey::builtin(p_type.builtin_type);
		case DT::NATIVE:
			return TypeKey::native(p_type.native_type);
		case DT::CLASS: {
			const String k = class_key(p_type.class_type, p_type.script_path);
			return k.is_empty() ? TypeKey() : TypeKey::script_class(StringName(k));
		}

		case DT::ENUM: {
			///we own the enum
			if (p_type.class_type != nullptr) {
				const String k = class_key(p_type.class_type, p_type.script_path);
				return k.is_empty() ? TypeKey() : TypeKey::enum_type(StringName(k + "." + String(p_type.enum_type)));
			}
			///chudot owns the enum
			return TypeKey::enum_type(p_type.native_type);
		}

		default:
			///could maybe track more stuff down the line but i dont feel like it rn
			///besides this should be everything from legacy so it should be fine
			return TypeKey();
	}
}

}///namespace RGIndex