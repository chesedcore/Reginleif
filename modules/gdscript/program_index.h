/**************************************************************************/
/*  program_index.h                                                       */
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


///rules for future updates:
///i. once a ProgramIndex is created, it is IMMUT!! readers only ever hold a Ref<ProgramIndex>.
///ii. the index should NEVER trigger re-analysis or re-comp!! it is built from FileSummary values only!
///iii. a FileSummary holds NO AST pointers. it is pure data, so it should be safe to hold.
///iv. Analyzer/compiler/VM go through IndexView, which records every Query plus a fingerprint of the resulr into a DependencySet.
///v. invalidation is done as re-fingerprinting the DependencySet. there is NO OTHER staleness mechanism.
#pragma once

#include "core/object/ref_counted.h"
#include "core/os/mutex.h"
#include "core/string/string_name.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/safe_refcount.h"
#include "core/templates/vector.h"
#include "core/variant/variant.h"

class GDScriptParser;

namespace RGIndex {

///you ever think, 'hmm, what'd i do with my life to get to this point?'
///yeah... yeah.
_FORCE_INLINE_ uint64_t hash_combine(uint64_t p_h, uint64_t p_v) {
	return p_h ^ (p_v + 0x9e3779b97f4a7c15ULL + (p_h << 6) + (p_h >> 2));
}

///quite literally just GDScript::canonicalize_path lmaoooo
String canonical_path(const String &p_path);

struct TypeKey {
	enum Kind : uint8_t {
		NONE,
		BUILTIN,            ///id   is Variant::Type
		NATIVE,             ///name is native class name
		SCRIPT_CLASS,       ///name is "<canonical path>[::Inner[::Inner]]"
		ENUM,               ///name is "<owner class key>.<Enum>" for script enums, "<Native>.<Enum>" for native enums.
	};
	Kind kind = NONE;
	uint32_t id = 0;
	StringName name;

	bool is_valid() const { return kind != NONE; }
	static TypeKey builtin(Variant::Type p_type) { return { BUILTIN, (uint32_t)p_type, StringName() }; }
	static TypeKey native(const StringName& p_class) { return { NATIVE, 0, p_class }; }
	static TypeKey script_class(const StringName& p_key) { return { SCRIPT_CLASS, 0, p_key }; }
	static TypeKey enum_type(const StringName& p_spelt) { return { ENUM, 0, p_spelt }; }

	bool operator==(const TypeKey& p_o) const { return kind == p_o.kind && id == p_o.id && name == p_o.name; }
	bool operator!=(const TypeKey& p_o) const { return !(*this == p_o); }
	uint32_t hash() const { return hash_murmur3_one_32(((uint32_t)kind << 24) ^ id, name.hash()); }
	uint64_t hash64() const { return hash_combine(((uint64_t)kind << 32) | id, name.hash()); }
	String debug_string() const;
};

struct TypeKeyHasher {
	static _FORCE_INLINE_ uint32_t hash(const TypeKey &p_k) { return p_k.hash(); }
};

struct TypeRef {
	enum Kind : uint8_t { VARIANT, VOID_TYPE, BUILTIN, NAMED, GENERIC_PARAM, TRAIT_BOUND };
	Kind kind = VARIANT;
	Variant::Type builtin = Variant::NIL; ///BUILTIN
	StringName name;                      ///NAMED: first ident in chain. GENERIC_PARAM: the parameter name
	Vector<StringName> chain_tail;        ///NAMED: `A.B.C` -> [B, C]
	bool callable_signature = false;
	Vector<TypeRef> args;                 ///Array[T], Dictionary[K, V], Cock[T]
	Vector<StringName> trait_bounds;      ///TRAIT_BOUND: `impl Cock + Balls`
	TypeKey resolved;                     ///NAMED only. !!REMEMBER!! invalid until the ProgramIndexBuilder resolves it!

	uint64_t hash() const;
	String debug_string() const;
};

struct MethodSig {
	Vector<TypeRef> param_types;
	Vector<StringName> param_names;
	Vector<String> param_defaults;
	TypeRef return_type;
	int default_arg_count = 0;
	bool is_vararg = false;
	bool is_static = false;
	uint64_t sig_hash = 0; ///ONLY from params,return and flags
};

uint64_t compute_sig_hash(const MethodSig& p_sig);

struct MethodDecl {
	StringName name;
	MethodSig sig;
	bool has_body = false;  ///false for required trait fns and abstract fns
	uint64_t body_hash = 0; ///hash of the source lines spanned by the fn (with signature!!)
	int start_line = 0;
	String doc;
};


struct BaseRef {
	enum Kind : uint8_t { IMPLICIT, PATH, NAME };
	Kind kind = IMPLICIT;          ///IMPLICIT implies RefCounted base
	String path;                   ///absolute simplified canonical path
	StringName name;               ///NAME is first ident
	Vector<StringName> chain_tail; ///`extends A.B.C` / `extends "x.gd".B.C` trailing parts
	Vector<TypeRef> generic_args;
};

struct ClassDecl {
	StringName key;         ///canonical. "<path>" for toplevel, "<path>::Inner::Innerer" for inner
	StringName parser_fqcn; ///ClassNode::fqcn spelling
	StringName outer_key;   ///empty for the toplevel class
	StringName local_name;  ///inner class ident, or class_name for the root (could be empty)
	StringName global_name; ///class_name. root only
	BaseRef base;
	bool is_abstract = false;
	Vector<StringName> generic_params;
	Vector<TypeRef> attach_to;
	Vector<MethodDecl> methods; ///fns declared directly in this class (NOT impls or trait methods!)
	HashSet<StringName> enum_names;
};

struct TraitDecl {
	StringName name;
	Vector<MethodDecl> required;
	Vector<MethodDecl> defaults;
	Vector<StringName> generic_params;
};

struct ImplDecl {
	StringName trait_name;
	bool explicit_target = false;
	TypeRef target;             ///valid iff explicit_target
	StringName scope_class_key; ///class whose body holds the impl, empty for impls in a trait file itself
	Vector<MethodDecl> provided;
	int line = 0;
};

struct FileSummary : public RefCounted {
	GDSOFTCLASS(FileSummary, RefCounted);

public:
	String path;               ///canonical
	uint64_t content_hash = 0;
	bool parse_failed = false; ///partial data is kept anyway
	bool is_trait_file = false;
	bool is_tool = false;
	Vector<ClassDecl> classes; ///a class precedes its inner classes (preorder)
	Vector<TraitDecl> traits;
	Vector<ImplDecl> impls;

	///!!`p_parser` MUST have been run with p_parse_body = true!!
	static Ref<FileSummary> build_from_parser(const GDScriptParser& p_parser, const String& p_path, const String& p_source);
	static Ref<FileSummary> build_from_source(const String& p_path, const String& p_source);
	static Ref<FileSummary> build_from_file(const String& p_path);
};

typedef uint64_t Fingerprint;

struct Query {
	enum Kind : uint8_t {
		IMPLEMENTS,           ///type, a=trait                  bool
		IMPL_METHOD,          ///type, a=method                 ImplHit
		IMPL_METHODS_OF_TRAIT,///type, a=trait                  set of fn names provided (explicit amd default)
		NO_OVERRIDE,          ///a=class key, b=method          bool. no reachable subclass declares it
		TRAIT_SIG,            ///a=trait, b=method              resolved signature and required/default
		TRAIT_EXISTS,         ///a=trait                        path lol
		INLINE_BODY,          ///a=class key, b=method          (body_hash, sig_hash) of that exact declaration
		SUBCLASS_SET,         ///a=class key                    set of reachable subclasses
	};

	Kind kind = IMPLEMENTS;
	TypeKey type;
	StringName a;
	StringName b;

	bool operator==(const Query& p_o) const { return kind == p_o.kind && type == p_o.type && a == p_o.a && b == p_o.b; }
	uint32_t hash() const;

	static Query implements(const TypeKey& p_type, const StringName& p_trait) { return { IMPLEMENTS, p_type, p_trait, StringName() }; }
	static Query impl_method(const TypeKey& p_type, const StringName& p_method) { return { IMPL_METHOD, p_type, p_method, StringName() }; }
	static Query impl_methods_of_trait(const TypeKey& p_type, const StringName& p_trait) { return { IMPL_METHODS_OF_TRAIT, p_type, p_trait, StringName() }; }
	static Query no_override(const StringName& p_class_key, const StringName& p_method) { return { NO_OVERRIDE, TypeKey(), p_class_key, p_method }; }
	static Query trait_sig(const StringName& p_trait, const StringName& p_method) { return { TRAIT_SIG, TypeKey(), p_trait, p_method }; }
	static Query trait_exists(const StringName& p_trait) { return { TRAIT_EXISTS, TypeKey(), p_trait, StringName() }; }
	static Query inline_body(const StringName& p_class_key, const StringName& p_method) { return { INLINE_BODY, TypeKey(), p_class_key, p_method }; }
	static Query subclass_set(const StringName& p_class_key) { return { SUBCLASS_SET, TypeKey(), p_class_key, StringName() }; }
};

struct QueryHasher {
	static _FORCE_INLINE_ uint32_t hash(const Query& p_q) { return p_q.hash(); }
};

///what one script assumed about the rest of the code. deduped by query
struct DependencySet {
	HashMap<Query, Fingerprint, QueryHasher> deps;
	bool has(const Query& p_q) const { return deps.has(p_q); }
	void add(const Query& p_q, Fingerprint p_fp) { deps.insert(p_q, p_fp); }
	void clear() { deps.clear(); }
};

struct ImplHit {
	bool found = false;
	StringName trait_name;
	String owning_path; ///file that declared the impl
	TypeKey impl_target;///the type the impl was declared on (may be an ancestor of the queried type)
	MethodDecl decl;    ///resolved signature amd body_hash. For default methods this is the TRAIT's decl!!
	bool is_default_method = false;
	int impl_line = 0;  ///line of the impl block itself
};

class ProgramIndex : public RefCounted {
	GDSOFTCLASS(ProgramIndex, RefCounted);

public:
	struct MethodHashes {
		uint64_t body = 0;
		uint64_t sig = 0;
	};
	struct ClassEntry {
		StringName key;
		StringName parser_fqcn;
		StringName outer_key;
		StringName local_name;
		StringName global_name;
		String path;
		TypeKey base; ///MUST be resolved. invalid if not!!
		bool is_abstract = false;
		Vector<TypeRef> attach_to;
		HashMap<StringName, MethodHashes> methods;
		HashSet<StringName> enums;
	};
	struct TraitEntry {
		StringName name;
		String path;
		HashMap<StringName, MethodDecl> required;
		HashMap<StringName, MethodDecl> defaults;
	};
	struct ImplEntry {
		StringName trait_name;
		String owning_path;
		TypeKey target;
		int line = 0;
		bool target_owned_by_file = false;
		HashMap<StringName, MethodDecl> provided; ///explicits AND trait defaults
		HashSet<StringName> default_provided;
	};
	struct Diagnostic {
		String path;
		int line = 0;
		String message;
		bool is_error = true;
	};

private:
	uint64_t epoch = 0;
	HashMap<String, Ref<FileSummary>> files;
	HashMap<StringName, ClassEntry> classes;                 ///by canonical key
	HashMap<StringName, StringName> alias;                   ///ClassNode::fqcn spelling to canonical key
	HashMap<StringName, StringName> global_class_key;        ///class_name to canonical key
	HashMap<StringName, HashSet<StringName>> direct_children;///canonical key to canonical keys of subclasses
	HashMap<StringName, TraitEntry> traits;
	HashMap<TypeKey, Vector<ImplEntry>, TypeKeyHasher> impls;
	HashMap<String, Vector<Diagnostic>> diagnostics_by_path;
	int unresolved_impl_targets = 0;

	friend class ProgramIndexBuilder;

public:
	uint64_t get_epoch() const { return epoch; }
	bool has_file(const String &p_path) const { return files.has(p_path); }
	int get_unresolved_impl_target_count() const { return unresolved_impl_targets; }
	const HashMap<TypeKey, Vector<ImplEntry>, TypeKeyHasher>& get_impls() const { return impls; }

	///raw lookups from this point on!! NOT dep-tracked!! analyzer/compiler MUST use IndexView instead!!
	///don't mess with these unless you know what the fuck you're doing!!
	StringName resolve_class_key(const StringName& p_spelling) const; ///accepts key or fqcn
	const ClassEntry* get_class(const StringName& p_spelling) const;
	const TraitEntry* get_trait(const StringName& p_name) const { return traits.getptr(p_name); }
	String get_path_for_class(const StringName& p_spelling) const;

	///follows most-derived order, also cycle safe
	void get_ancestors_inclusive(const TypeKey& p_type, Vector<TypeKey>& r_chain) const;
	void get_reachable_subclasses(const StringName& p_spelling, Vector<StringName>& r_keys) const;

	bool implements(const TypeKey& p_type, const StringName& p_trait) const;
	ImplHit find_impl_method(const TypeKey& p_type, const StringName& p_method) const;
	bool no_reachable_subclass_overrides(const StringName& p_spelling, const StringName& p_method) const;
	bool get_inline_body_hashes(const StringName& p_spelling, const StringName& p_method, MethodHashes& r_hashes) const;

	const Vector<Diagnostic>* get_diagnostics_for(const String& p_path) const { return diagnostics_by_path.getptr(p_path); }

	Fingerprint fingerprint(const Query& p_q) const;
};

///the only handle the analyzer/compiler/VM get
///!!!NOT THREAD SAFE by itself!!! make sure to only make one view per run!!
class IndexView {
	Ref<ProgramIndex> snapshot;
	DependencySet* deps = nullptr; ///nullptr is don't record

	void _record(const Query& p_q) const;

public:
	IndexView(Ref<ProgramIndex> p_snapshot, DependencySet* p_deps) :
			snapshot(p_snapshot), deps(p_deps) {}

	bool implements(const TypeKey& p_type, const StringName& p_trait) const;
	ImplHit find_impl_method(const TypeKey& p_type, const StringName& p_method) const;
	bool no_reachable_subclass_overrides(const StringName& p_class, const StringName& p_method) const;
	const ProgramIndex::TraitEntry* get_trait(const StringName& p_name) const; ///also records TRAIT_EXISTS
	bool get_inline_body_hashes(const StringName& p_class, const StringName& p_method, ProgramIndex::MethodHashes& r_hashes) const;

	const ProgramIndex* raw() const { return snapshot.ptr(); }
};

class ProgramIndexService {
	mutable Mutex mutex;
	Mutex commit_mutex;
	Mutex populate_mutex;

	Ref<ProgramIndex> current;
	HashMap<String, Ref<FileSummary>> committed;
	HashMap<String, Ref<FileSummary>> staged; ///null Ref means removal
	HashMap<String, DependencySet> script_deps;
	Vector<String> pending_stale;
	uint64_t epoch_counter = 0;
	SafeFlag populated;

	static ProgramIndexService* singleton;

	static void _collect_script_paths(const String& p_dir, Vector<String>& r_paths);

public:
	static ProgramIndexService* get_singleton() { return singleton; }

	Ref<ProgramIndex> get_snapshot() const;

	///blocks until the index covers the project. call this ONCE at the entry of an analysis pass
	void ensure_populated(const String& p_root = "res://");
	bool is_populated() const { return populated.is_set(); }

	void stage_file(const Ref<FileSummary>& p_summary);
	void stage_removal(const String& p_path);
	int stage_removals_not_in(const HashSet<String>& p_live);
	Vector<String> commit();

	void set_dependencies(const String& p_script_path, const DependencySet& p_deps);
	void forget_script(const String &p_script_path);
	Vector<String> take_stale();

	ProgramIndexService();
	~ProgramIndexService();
};

} /// namespace RGIndex