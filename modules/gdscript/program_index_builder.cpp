/**************************************************************************/
/*  program_index_builder.cpp                                             */
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

#include "program_index.h"

#include "core/io/dir_access.h"
#include "core/object/class_db.h"

namespace RGIndex {
///did i name it so because "Reginleif Index"...? ah well, i already forgot, so...

class ProgramIndexBuilder {
	Ref<ProgramIndex> idx;

	struct Resolved {
		TypeKey key;
		String owner_path;
	};

	void _diag(const String& p_path, int p_line, const String& p_message, bool p_is_error = true) {
		ProgramIndex::Diagnostic d;
		d.path = p_path;
		d.line = p_line;
		d.message = p_message;
		d.is_error = p_is_error;
		idx->diagnostics_by_path[p_path].push_back(d);
	}

	Resolved _resolve_named(const StringName& p_first, const Vector<StringName>& p_tail, const StringName& p_scope) const;
	void _resolve_ref(TypeRef& r_ref, const StringName& p_scope) const;
	void _resolve_decl(MethodDecl& r_decl, const StringName& p_scope) const;

	void _pass_declarations(const Vector<String>& p_paths);
	void _pass_bases(const Vector<String>& p_paths);
	void _pass_attach(const Vector<String>& p_paths);
	void _pass_traits();
	void _pass_impls(const Vector<String>& p_paths);
	void _validate();

public:
	static Ref<ProgramIndex> build(const HashMap<String, Ref<FileSummary>>& p_files, uint64_t p_epoch);
};

ProgramIndexBuilder::Resolved ProgramIndexBuilder::_resolve_named(const StringName& p_first, const Vector<StringName>& p_tail, const StringName& p_scope) const {
	Resolved out;
	TypeKey cur;

	StringName s = p_scope;
	for (int guard = 0; s != StringName() && guard < 64; guard++) {
		const ProgramIndex::ClassEntry* se = idx->classes.getptr(s);
		if (se == nullptr) {
			break;
		}
		const StringName inner = StringName(String(s) + "::" + String(p_first));
		if (idx->classes.has(inner)) {
			cur = TypeKey::script_class(inner);
			break;
		}

		if (se->enums.has(p_first)) {
			cur = TypeKey::enum_type(StringName(String(s) + "." + String(p_first)));
			break;
		}

		s = se->outer_key;
	}

	if (!cur.is_valid()) {
		const StringName* g = idx->global_class_key.getptr(p_first);
		if (g != nullptr) {
			cur = TypeKey::script_class(*g);
			    ///might want to replace this shit below when they phase out ClassDB
		} else if (ClassDB::class_exists(p_first)) {
			cur = TypeKey::native(p_first);
		}
	}

	for (const StringName& t : p_tail) {
		if (!cur.is_valid()) {
			break;
		}
		if (cur.kind == TypeKey::SCRIPT_CLASS) {
			const StringName inner = StringName(String(cur.name) + "::" + String(t));
			const ProgramIndex::ClassEntry* ce = idx->classes.getptr(cur.name);
			if (idx->classes.has(inner)) {
				cur = TypeKey::script_class(inner);
			} else if (ce != nullptr && ce->enums.has(t)) {
				cur = TypeKey::enum_type(StringName(String(cur.name) + "." + String(t)));
			} else {
				cur = TypeKey();
			}
		} else if (cur.kind == TypeKey::NATIVE && ClassDB::has_enum(cur.name, t)) {
			cur = TypeKey::enum_type(StringName(String(cur.name) + "." + String(t)));
		} else {
			cur = TypeKey();
		}
	}

	out.key = cur;
	if (cur.kind == TypeKey::SCRIPT_CLASS) {

		const ProgramIndex::ClassEntry* ce = idx->classes.getptr(cur.name);
		out.owner_path = ce != nullptr ? ce->path : String();

	} else if (cur.kind == TypeKey::ENUM) {

		const String n = cur.name;
		const int dot = n.rfind(".");

		const ProgramIndex::ClassEntry* ce = dot > 0 ? idx->classes.getptr(StringName(n.substr(0, dot))) : nullptr;

		out.owner_path = ce != nullptr ? ce->path : String(); ///native enums dont have an owning file
	}
	return out;
}

void ProgramIndexBuilder::_resolve_ref(TypeRef& r_ref, const StringName& p_scope) const {
	for (int i = 0; i < r_ref.args.size(); i++) {
		_resolve_ref(r_ref.args.write[i], p_scope);
	}
	if (r_ref.kind == TypeRef::NAMED) {
		r_ref.resolved = _resolve_named(r_ref.name, r_ref.chain_tail, p_scope).key;
	}
}

void ProgramIndexBuilder::_resolve_decl(MethodDecl& r_decl, const StringName& p_scope) const {
	for (int i = 0; i < r_decl.sig.param_types.size(); i++) {
		_resolve_ref(r_decl.sig.param_types.write[i], p_scope);
	}
	_resolve_ref(r_decl.sig.return_type, p_scope);
	r_decl.sig.sig_hash = compute_sig_hash(r_decl.sig);
}

void ProgramIndexBuilder::_pass_declarations(const Vector<String>& p_paths) {
	for (const String& path : p_paths) {
		const FileSummary& fs = *idx->files[path].ptr();

		for (const ClassDecl& c : fs.classes) {
			if (idx->classes.has(c.key)) {
				_diag(path, 0, vformat(R"(Class "%s" is declared more than once.)", String(c.key)));
				continue;
			}

			ProgramIndex::ClassEntry e;
			e.key = c.key;
			e.parser_fqcn = c.parser_fqcn;
			e.outer_key = c.outer_key;
			e.local_name = c.local_name;
			e.global_name = c.global_name;
			e.path = path;
			e.is_abstract = c.is_abstract;
			e.enums = c.enum_names;

			for (const MethodDecl& m : c.methods) {
				ProgramIndex::MethodHashes h;
				h.body = m.body_hash;
				h.sig = m.sig.sig_hash;
				e.methods.insert(m.name, h);
			}

			idx->classes.insert(c.key, e);

			if (c.parser_fqcn != StringName() && c.parser_fqcn != c.key) {
				idx->alias.insert(c.parser_fqcn, c.key);
			}

			if (c.global_name != StringName()) {
				const StringName* existing = idx->global_class_key.getptr(c.global_name);
				if (existing != nullptr) {
					_diag(path, 0, vformat(R"(Global class name "%s" is also declared in "%s".)", String(c.global_name), idx->classes[*existing].path));
				} else {
					idx->global_class_key.insert(c.global_name, c.key);
				}
			}
		}

		for (const TraitDecl& t : fs.traits) {
			const ProgramIndex::TraitEntry* existing = idx->traits.getptr(t.name);
			if (existing != nullptr) {
				_diag(path, 0, vformat(R"(Trait "%s" is also declared in "%s".)", String(t.name), existing->path));
				continue;
			}
			ProgramIndex::TraitEntry te;
			te.name = t.name;
			te.path = path;
			for (const MethodDecl& m : t.required) {
				te.required.insert(m.name, m);
			}
			for (const MethodDecl& m : t.defaults) {
				te.defaults.insert(m.name, m);
			}
			idx->traits.insert(t.name, te);
		}
	}
}

void ProgramIndexBuilder::_pass_bases(const Vector<String>& p_paths) {
	for (const String& path : p_paths) {
		const FileSummary& fs = *idx->files[path].ptr();
		for (const ClassDecl &c : fs.classes) {
			ProgramIndex::ClassEntry* e = idx->classes.getptr(c.key);
			if (e == nullptr || e->path != path) {
				continue; ///duped decl was already caught above (hopefully)
			}

			TypeKey base;
			switch (c.base.kind) {
				case BaseRef::IMPLICIT: {
					base = TypeKey::native(SNAME("RefCounted"));
				} break;
				case BaseRef::PATH: {
					StringName k = StringName(c.base.path);
					bool ok = idx->classes.has(k);
					for (const StringName &t : c.base.chain_tail) {
						if (!ok) {
							break;
						}
						k = StringName(String(k) + "::" + String(t));
						ok = idx->classes.has(k);
					}
					if (ok) {
						base = TypeKey::script_class(k);
					}
				} break;
				case BaseRef::NAME: {
					StringName s = c.outer_key;
					for (int guard = 0; s != StringName() && guard < 64; guard++) {
						const StringName cand = StringName(String(s) + "::" + String(c.base.name));
						if (idx->classes.has(cand)) {
							base = TypeKey::script_class(cand);
							break;
						}
						const ProgramIndex::ClassEntry* se = idx->classes.getptr(s);
						if (se == nullptr) {
							break;
						}
						s = se->outer_key;
					}
					if (!base.is_valid()) {
						const StringName* g = idx->global_class_key.getptr(c.base.name);
						if (g != nullptr) {
							base = TypeKey::script_class(*g);
						} else if (ClassDB::class_exists(c.base.name)) {
							base = TypeKey::native(c.base.name);
						}
					}
					for (const StringName& t : c.base.chain_tail) {
						if (base.kind != TypeKey::SCRIPT_CLASS) {
							base = TypeKey();
							break;
						}
						const StringName k = StringName(String(base.name) + "::" + String(t));
						base = idx->classes.has(k) ? TypeKey::script_class(k) : TypeKey();
					}
				} break;
			}

			///the analyser needs to report this
			e->base = base;
			if (base.kind == TypeKey::SCRIPT_CLASS) {
				idx->direct_children[base.name].insert(c.key);
			}
		}
	}
}

//i'm genuinely so tired, boss... i hope, by the end of making this immut snapshot table,
//future features are easier to make without running into a billion re-entrancy issues... 

void ProgramIndexBuilder::_pass_attach(const Vector<String>& p_paths) {
	for (const String& path : p_paths) {
		const FileSummary& fs = *idx->files[path].ptr();
		for (const ClassDecl& c : fs.classes) {
			ProgramIndex::ClassEntry* e = idx->classes.getptr(c.key);
			if (e == nullptr || e->path != path) {
				continue;
			}
			e->attach_to = c.attach_to;
			for (int i = 0; i < e->attach_to.size(); i++) {
				_resolve_ref(e->attach_to.write[i], c.key);
			}
		}
	}
}

void ProgramIndexBuilder::_pass_traits() {
	for (KeyValue<StringName, ProgramIndex::TraitEntry>& T : idx->traits) {
		for (KeyValue<StringName, MethodDecl>& M : T.value.required) {
			_resolve_decl(M.value, StringName());
		}
		for (KeyValue<StringName, MethodDecl>& M : T.value.defaults) {
			_resolve_decl(M.value, StringName());
		}
	}
}

void ProgramIndexBuilder::_pass_impls(const Vector<String>& p_paths) {
	for (const String& path : p_paths) {
		const FileSummary& fs = *idx->files[path].ptr();
		for (const ImplDecl& d : fs.impls) {
			const ProgramIndex::TraitEntry* te = idx->traits.getptr(d.trait_name);
			if (te == nullptr) {
				_diag(path, d.line, vformat(R"(Could not find trait "%s".)", String(d.trait_name)));
				continue;
			}

			TypeKey target;
			String owner;
			if (d.explicit_target) {
				if (d.target.kind == TypeRef::NAMED) {
					const Resolved r = _resolve_named(d.target.name, d.target.chain_tail, d.scope_class_key);
					target = r.key;
					owner = r.owner_path;
				} else if (d.target.kind == TypeRef::BUILTIN) {
					target = TypeKey::builtin(d.target.builtin);
				}
			} else if (d.scope_class_key != StringName()) {
				target = TypeKey::script_class(d.scope_class_key);
				owner = path;
			}

			if (!target.is_valid()) {
				idx->unresolved_impl_targets++;
				_diag(path, d.line, vformat(R"(Index could not resolve the target type of this impl ("%s").)", d.explicit_target ? d.target.debug_string() : String("<in-class>")), false);
				continue;
			}

			ProgramIndex::ImplEntry en;
			en.trait_name = d.trait_name;
			en.owning_path = path;
			en.target = target;
			en.line = d.line;
			en.target_owned_by_file = owner == path;

			for (const MethodDecl& src : d.provided) {
				if (!te->required.has(src.name) && !te->defaults.has(src.name)) {
					_diag(path, src.start_line, vformat(R"(Trait "%s" has no method "%s" to implement.)", String(d.trait_name), String(src.name)));
					continue;
				}
				MethodDecl m = src;
				_resolve_decl(m, d.scope_class_key);
				en.provided.insert(m.name, m);
			}


			for (const KeyValue<StringName, MethodDecl>& D : te->defaults) {
				if (!en.provided.has(D.key)) {
					en.provided.insert(D.key, D.value);
					en.default_provided.insert(D.key);
				}
			}

			for (const KeyValue<StringName, MethodDecl>& R : te->required) {
				if (!en.provided.has(R.key)) {
					_diag(path, d.line, vformat(R"("impl" for trait "%s" does not implement required method "%s".)", String(d.trait_name), String(R.key)));
				}
			}

			idx->impls[target].push_back(en);
		}
	}
}

namespace {
struct ImplEntryLess {
	bool operator()(const ProgramIndex::ImplEntry& p_a, const ProgramIndex::ImplEntry& p_b) const {
		if (p_a.owning_path != p_b.owning_path) {
			return p_a.owning_path < p_b.owning_path;
		}
		if (p_a.trait_name != p_b.trait_name) {
			return String(p_a.trait_name) < String(p_b.trait_name);
		}
		return p_a.line < p_b.line;
	}
};
} ///namespace

void ProgramIndexBuilder::_validate() {
	///catch cyclic inheritance
	for (const KeyValue<StringName, ProgramIndex::ClassEntry>& C : idx->classes) {
		HashSet<StringName> seen;
		seen.insert(C.key);
		TypeKey cur = C.value.base;
		while (cur.kind == TypeKey::SCRIPT_CLASS) {
			if (seen.has(cur.name)) {
				_diag(C.value.path, 0, vformat(R"(Cyclic inheritance involving class "%s".)", String(C.key)));
				break;
			}
			seen.insert(cur.name);
			const ProgramIndex::ClassEntry* n = idx->classes.getptr(cur.name);
			if (n == nullptr) {
				break;
			}
			cur = n->base;
		}
	}

	for (const KeyValue<StringName, ProgramIndex::TraitEntry>& T : idx->traits) {
		const StringName* g = idx->global_class_key.getptr(T.key);
		if (g != nullptr) {
			_diag(T.value.path, 0, vformat(R"(Trait "%s" hides a global script class.)", String(T.key)));
		}
	}

	for (KeyValue<TypeKey, Vector<ProgramIndex::ImplEntry>>& I : idx->impls) {
		Vector<ProgramIndex::ImplEntry>& entries = I.value;
		entries.sort_custom<ImplEntryLess>();

		for (int j = 0; j < entries.size(); j++) {
			const ProgramIndex::ImplEntry& ej = entries[j];

			const ProgramIndex::TraitEntry* te = idx->traits.getptr(ej.trait_name);
			if (te != nullptr && te->path != ej.owning_path && !ej.target_owned_by_file) {
				_diag(ej.owning_path, ej.line, vformat(R"(Cannot "impl" trait "%s" for this type: neither the trait nor the target type is declared in this file.)", String(ej.trait_name)));
			}

			for (int i = 0; i < j; i++) {

				const ProgramIndex::ImplEntry& ei = entries[i];
				if (ei.trait_name == ej.trait_name) {
					_diag(ej.owning_path, ej.line, vformat(R"(Duplicate "impl" of trait "%s" for the same type "%s" (also in "%s").)", String(ej.trait_name), I.key.debug_string(), ei.owning_path));
					continue;
				}

				for (const KeyValue<StringName, MethodDecl>& M : ej.provided) {
					if (ei.provided.has(M.key)) {
						const String tag = ei.owning_path != ej.owning_path ? String("[cross-file] ") : String();
						_diag(ej.owning_path, ej.line, tag + vformat(R"(Method "%s" is provided by both trait "%s" and trait "%s" (in "%s") for the same type, so trait dispatch is ambiguous.)", String(M.key), String(ej.trait_name), String(ei.trait_name), ei.owning_path));
					}
				}

			}
		}
	}
}

///could probably use some optimising if the profiler demands it, but i THINK this should be fine for most cases?
///like a few miliseconds at best for a thousand files?

Ref<ProgramIndex> ProgramIndexBuilder::build(const HashMap<String, Ref<FileSummary>>& p_files, uint64_t p_epoch) {
	ProgramIndexBuilder b;
	b.idx.instantiate();
	b.idx->epoch = p_epoch;

	Vector<String> paths;
	for (const KeyValue<String, Ref<FileSummary>>& E : p_files) {
		if (E.value.is_valid()) {
			b.idx->files.insert(E.key, E.value);
			paths.push_back(E.key);
		}
	}
	paths.sort(); ///done to keep diagnostics deterministic in order

	b._pass_declarations(paths);
	b._pass_bases(paths);
	b._pass_attach(paths);
	b._pass_traits();
	b._pass_impls(paths);
	b._validate();
	return b.idx;
}

///queries from here on out

StringName ProgramIndex::resolve_class_key(const StringName& p_spelling) const {
	if (classes.has(p_spelling)) {
		return p_spelling;
	}
	const StringName* a = alias.getptr(p_spelling);
	return a != nullptr ? *a : StringName();
}

const ProgramIndex::ClassEntry* ProgramIndex::get_class(const StringName& p_spelling) const {
	const StringName key = resolve_class_key(p_spelling);
	return key == StringName() ? nullptr : classes.getptr(key);
}

String ProgramIndex::get_path_for_class(const StringName& p_spelling) const {
	const ClassEntry* c = get_class(p_spelling);
	return c != nullptr ? c->path : String();
}

void ProgramIndex::get_ancestors_inclusive(const TypeKey& p_type, Vector<TypeKey>& r_chain) const {
	TypeKey cur = p_type;
	HashSet<StringName> visited;
	for (int guard = 0; cur.is_valid() && guard < 512; guard++) {
		if (cur.kind == TypeKey::SCRIPT_CLASS) {

			if (visited.has(cur.name)) {
				break; ///cycle! caught in _validate prior
			}

			visited.insert(cur.name);
			r_chain.push_back(cur);
			const ClassEntry* c = classes.getptr(cur.name);

			if (c == nullptr) {
				break;
			}

			cur = c->base;

		} else if (cur.kind == TypeKey::NATIVE) {
			r_chain.push_back(cur);
			const StringName parent = ClassDB::get_parent_class(cur.name);
			if (parent == StringName()) {
				break;
			}
			cur = TypeKey::native(parent);
		} else {
			r_chain.push_back(cur);
			break;
		}
	}
}

void ProgramIndex::get_reachable_subclasses(const StringName& p_spelling, Vector<StringName>& r_keys) const {
	const StringName root = resolve_class_key(p_spelling);
	if (root == StringName()) {
		return;
	}
	HashSet<StringName> visited;
	visited.insert(root);
	Vector<StringName> frontier;
	frontier.push_back(root);

	while (!frontier.is_empty()) {
		const StringName current = frontier[frontier.size() - 1];
		frontier.remove_at(frontier.size() - 1);

		///i ran out of names bro
		const HashSet<StringName>* kids = direct_children.getptr(current);
		if (kids == nullptr) {
			continue;
		}
		for (const StringName& k : *kids) {
			if (!visited.has(k)) {
				visited.insert(k);
				frontier.push_back(k);
				r_keys.push_back(k);
			}
		}
	}
}

bool ProgramIndex::implements(const TypeKey& p_type, const StringName& p_trait) const {
	Vector<TypeKey> chain;
	get_ancestors_inclusive(p_type, chain);
	for (const TypeKey& t : chain) {

		const Vector<ImplEntry>* entries = impls.getptr(t);
		if (entries == nullptr) {
			continue;
		}

		for (const ImplEntry& e : *entries) {
			if (e.trait_name == p_trait) {
				return true;
			}
		}

	}
	return false;
}

ImplHit ProgramIndex::find_impl_method(const TypeKey& p_type, const StringName& p_method) const {
	ImplHit hit;
	Vector<TypeKey> chain;
	get_ancestors_inclusive(p_type, chain);
	for (const TypeKey& t : chain) { ///most derived first order

		const Vector<ImplEntry>* entries = impls.getptr(t);
		if (entries == nullptr) {
			continue;
		}
		for (const ImplEntry& e : *entries) {
			const MethodDecl* m = e.provided.getptr(p_method);
			if (m == nullptr) {
				continue;
			}

			hit.found = true;
			hit.trait_name = e.trait_name;
			hit.owning_path = e.owning_path;
			hit.impl_target = e.target;
			hit.decl = *m;
			hit.is_default_method = e.default_provided.has(p_method);
			hit.impl_line = e.line;
			return hit;
		}
	}
	return hit;
}

bool ProgramIndex::no_reachable_subclass_overrides(const StringName& p_spelling, const StringName& p_method) const {
	if (get_class(p_spelling) == nullptr) {
		return false; ///ace combat 7 skies unknown
	}

	Vector<StringName> subs;
	get_reachable_subclasses(p_spelling, subs);
	for (const StringName& k : subs) {
		const ClassEntry* c = classes.getptr(k);
		if (c != nullptr && c->methods.has(p_method)) {
			return false;
		}
	}
	return true;
}

bool ProgramIndex::get_inline_body_hashes(const StringName& p_spelling, const StringName& p_method, MethodHashes& r_hashes) const {
	const ClassEntry* c = get_class(p_spelling);
	if (c == nullptr) {
		return false;
	}
	const MethodHashes* h = c->methods.getptr(p_method);
	if (h == nullptr) {
		return false;
	}
	r_hashes = *h;
	return true;
}

Fingerprint ProgramIndex::fingerprint(const Query& p_q) const {
	switch (p_q.kind) {
		case Query::IMPLEMENTS:
			return implements(p_q.type, p_q.a) ? 1 : 0;

		case Query::IMPL_METHOD: {
			const ImplHit h = find_impl_method(p_q.type, p_q.a);
			if (!h.found) {
				return 0;
			}
		
			uint64_t f = hash_combine(1, h.trait_name.hash());
			f = hash_combine(f, h.impl_target.hash64());
			f = hash_combine(f, h.decl.sig.sig_hash);
			f = hash_combine(f, h.decl.body_hash);
			f = hash_combine(f, h.is_default_method ? 1 : 0);
			f = hash_combine(f, h.owning_path.hash());
			return f | 1ULL;
		}

		case Query::IMPL_METHODS_OF_TRAIT: {
			Vector<TypeKey> chain;
			get_ancestors_inclusive(p_q.type, chain);
			HashSet<StringName> names;
			for (const TypeKey& t : chain) {
				const Vector<ImplEntry>* entries = impls.getptr(t);
				if (entries == nullptr) {
					continue;
				}
				for (const ImplEntry& e : *entries) {
					if (e.trait_name == p_q.a) {
						for (const KeyValue<StringName, MethodDecl>& M : e.provided) {
							names.insert(M.key);
						}
					}
				}
			}

			uint64_t sum = 0;
			for (const StringName& n : names) {
				sum += hash_combine(0x51ULL, n.hash());
			}
			return hash_combine((uint64_t)names.size(), sum);
		}

		case Query::NO_OVERRIDE:
			return no_reachable_subclass_overrides(p_q.a, p_q.b) ? 1 : 0;

		case Query::TRAIT_SIG: {
			const TraitEntry* t = traits.getptr(p_q.a);
			if (t == nullptr) {
				return 0;
			}
			const MethodDecl* m = t->required.getptr(p_q.b);
			bool is_default = false;
			if (m == nullptr) {
				m = t->defaults.getptr(p_q.b);
				is_default = true;
			}
			return m == nullptr ? 0 : (hash_combine(m->sig.sig_hash, is_default ? 1 : 0) | 1ULL);
		}

		case Query::TRAIT_EXISTS: {
			const TraitEntry* t = traits.getptr(p_q.a);
			return t == nullptr ? 0 : (hash_combine(1, t->path.hash()) | 1ULL);
		}

		case Query::INLINE_BODY: {
			MethodHashes h;
			if (!get_inline_body_hashes(p_q.a, p_q.b, h)) {
				return 0;
			}
			return hash_combine(h.body, h.sig) | 1ULL;
		}

		case Query::SUBCLASS_SET: {
			Vector<StringName> subs;
			get_reachable_subclasses(p_q.a, subs);
			uint64_t sum = 0;
			for (const StringName& k : subs) {
				sum += hash_combine(0x77ULL, k.hash());
			}
			return hash_combine((uint64_t)subs.size(), sum);
		}
	}
	return 0;
}

///views from here on out

void IndexView::_record(const Query& p_q) const {
	if (deps != nullptr && !deps->has(p_q)) {
		deps->add(p_q, snapshot->fingerprint(p_q));
	}
}

bool IndexView::implements(const TypeKey& p_type, const StringName& p_trait) const {
	_record(Query::implements(p_type, p_trait));
	return snapshot->implements(p_type, p_trait);
}

ImplHit IndexView::find_impl_method(const TypeKey& p_type, const StringName& p_method) const {
	_record(Query::impl_method(p_type, p_method));
	return snapshot->find_impl_method(p_type, p_method);
}

bool IndexView::no_reachable_subclass_overrides(const StringName& p_class, const StringName& p_method) const {
	const StringName key = snapshot->resolve_class_key(p_class);
	_record(Query::no_override(key != StringName() ? key : p_class, p_method));
	return snapshot->no_reachable_subclass_overrides(p_class, p_method);
}

const ProgramIndex::TraitEntry* IndexView::get_trait(const StringName& p_name) const {
	_record(Query::trait_exists(p_name));
	return snapshot->get_trait(p_name);
}

bool IndexView::get_inline_body_hashes(const StringName& p_class, const StringName& p_method, ProgramIndex::MethodHashes& r_hashes) const {
	const StringName key = snapshot->resolve_class_key(p_class);
	_record(Query::inline_body(key != StringName() ? key : p_class, p_method));
	return snapshot->get_inline_body_hashes(p_class, p_method, r_hashes);
}

ProgramIndexService* ProgramIndexService::singleton = nullptr;

ProgramIndexService::ProgramIndexService() {
	singleton = this;
	current.instantiate(); // empty index, so readers never null-check
}

ProgramIndexService::~ProgramIndexService() {
	singleton = nullptr;
}

Ref<ProgramIndex> ProgramIndexService::get_snapshot() const {
	MutexLock lock(mutex);
	return current;
}

void ProgramIndexService::_collect_script_paths(const String& p_dir, Vector<String>& r_paths) {
	Error err = OK;
	Ref<DirAccess> dir = DirAccess::open(p_dir, &err);
	if (err != OK || dir.is_null() || dir->file_exists(".gdignore")) {
		return;
	}

	dir->list_dir_begin();
	String f = dir->get_next();
	while (!f.is_empty()) {
		if (dir->current_is_dir()) {
			if (!f.begins_with(".")) {
				_collect_script_paths(p_dir.path_join(f), r_paths);
			}
		} else {
			String name = f.ends_with(".remap") ? f.trim_suffix(".remap") : f;
			if (name.ends_with(".gd") || name.ends_with(".gdc")) {
				r_paths.push_back(p_dir.path_join(name));
			}
		}
		f = dir->get_next();
	}

	dir->list_dir_end();
}

void ProgramIndexService::ensure_populated(const String& p_root) {
	if (populated.is_set()) {
		commit(); ///apply anything the editor scanned since
		return;
	}

	///!!BLOCK!!
	///concurrent callers MUST stop here until the index is done

	MutexLock populate_lock(populate_mutex);
	if (populated.is_set()) {
		return;
	}

	Vector<String> paths;
	_collect_script_paths(p_root, paths);
	HashSet<String> seen;
	for (const String& p : paths) {
		const String c = canonical_path(p);

		if (seen.has(c)) {
			continue;
		}

		seen.insert(c);
		{
			MutexLock lock(mutex);
			if (committed.has(c) || staged.has(c)) {
				continue;
			}
		}

		Ref<FileSummary> s = FileSummary::build_from_file(p);
		if (s.is_valid()) {
			stage_file(s);
		}
	}

	commit();
	populated.set();
}

void ProgramIndexService::stage_file(const Ref<FileSummary> &p_summary) {
	ERR_FAIL_COND(p_summary.is_null());
	MutexLock lock(mutex);
	const Ref<FileSummary>* c = committed.getptr(p_summary->path);
	if (!staged.has(p_summary->path) && c != nullptr && c->is_valid() && (*c)->content_hash == p_summary->content_hash) {
		return;
	}
	staged[p_summary->path] = p_summary;
}

void ProgramIndexService::stage_removal(const String& p_path) {
	MutexLock lock(mutex);
	const String c = canonical_path(p_path);
	if (committed.has(c) || staged.has(c)) {
		staged[c] = Ref<FileSummary>();
	}
}

int ProgramIndexService::stage_removals_not_in(const HashSet<String>& p_live) {
	MutexLock lock(mutex);

	int removed = 0;
	for (const KeyValue<String, Ref<FileSummary>>& E : committed) {
		if (!p_live.has(E.key)) {
			staged[E.key] = Ref<FileSummary>();
			removed++;
		}
	}

	for (KeyValue<String, Ref<FileSummary>>& E : staged) {
		if (!p_live.has(E.key)) {
			E.value = Ref<FileSummary>();
		}
	}

	return removed;
}

Vector<String> ProgramIndexService::commit() {
	MutexLock commit_lock(commit_mutex);

	HashMap<String, Ref<FileSummary>> next;
	uint64_t epoch = 0;
	{
		MutexLock lock(mutex);
		if (staged.is_empty()) {
			return Vector<String>();
		}
		for (const KeyValue<String, Ref<FileSummary>>& E : staged) {
			if (E.value.is_valid()) {
				committed[E.key] = E.value;
			} else {
				committed.erase(E.key);
			}
		}
		staged.clear();
		next = committed;
		epoch = ++epoch_counter; ///i see what you mean now setzer-san,,,
	}

	///reminder that this can probably be improved by making it incremental
	Ref<ProgramIndex> built = ProgramIndexBuilder::build(next, epoch);

	Vector<String> stale;
	{
		MutexLock lock(mutex);
		current = built;
		for (const KeyValue<String, DependencySet>& S : script_deps) {
			bool holds = true;
			for (const KeyValue<Query, Fingerprint>& D : S.value.deps) {
				if (built->fingerprint(D.key) != D.value) {
					holds = false;
					break;
				}
			}

			if (!holds) {
				stale.push_back(S.key);
				if (!pending_stale.has(S.key)) {
					pending_stale.push_back(S.key);
				}
			}
		}
	}
	return stale;
}

void ProgramIndexService::set_dependencies(const String& p_script_path, const DependencySet& p_deps) {
	MutexLock lock(mutex);
	script_deps[canonical_path(p_script_path)] = p_deps;
}

void ProgramIndexService::forget_script(const String& p_script_path) {
	MutexLock lock(mutex);
	script_deps.erase(canonical_path(p_script_path));
}

Vector<String> ProgramIndexService::take_stale() {
	MutexLock lock(mutex);
	Vector<String> out = pending_stale;
	pending_stale.clear();
	return out;
}

}/// namespace RGIndex