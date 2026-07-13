/**************************************************************************/
/*  gdscript_cache.cpp                                                    */
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

#include "gdscript_cache.h"

#include "gdscript.h"
#include "gdscript_analyzer.h"
#include "gdscript_compiler.h"
#include "gdscript_parser.h"
#include "gdscript_trait.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/templates/rb_set.h"
#include "core/templates/vector.h"

GDScriptParserRef::Status GDScriptParserRef::get_status() const {
	return status;
}

///
GDScriptParserRef::TraitStatus GDScriptParserRef::get_trait_status() const {
	return trait_status;
}

String GDScriptParserRef::get_path() const {
	return path;
}

uint32_t GDScriptParserRef::get_source_hash() const {
	return source_hash;
}

GDScriptParser *GDScriptParserRef::get_parser() {
	if (parser == nullptr) {
		parser = memnew(GDScriptParser);
	}
	return parser;
}

GDScriptAnalyzer *GDScriptParserRef::get_analyzer() {
	if (analyzer == nullptr) {
		analyzer = memnew(GDScriptAnalyzer(get_parser()));
	}
	return analyzer;
}

Error GDScriptParserRef::raise_status(Status p_new_status) {
	ERR_FAIL_COND_V(clearing, ERR_BUG);
	ERR_FAIL_COND_V(parser == nullptr && status != EMPTY, ERR_BUG);

	if (p_new_status < status) {
		return OK;
	}

	while (result == OK && p_new_status > status) {
		switch (status) {
			case EMPTY: {
				// Calling parse will clear the parser, which can destruct another GDScriptParserRef which can clear the last reference to the script with this path, calling remove_script, which clears this GDScriptParserRef.
				// It's ok if its the first thing done here.
				get_parser()->clear();
				status = PARSED;
				String remapped_path = ResourceLoader::path_remap(path);
				if (remapped_path.has_extension("gdc")) {
					Vector<uint8_t> tokens = GDScriptCache::get_binary_tokens(remapped_path);
					source_hash = hash_djb2_buffer(tokens.ptr(), tokens.size());
					result = get_parser()->parse_binary(tokens, path);
				} else {
					String source = GDScriptCache::get_source_code(remapped_path);
					source_hash = source.hash();
					result = get_parser()->parse(source, path, false);
				}
			} break;
			case PARSED: {
				status = INHERITANCE_SOLVED;
				result = get_analyzer()->resolve_inheritance();
			} break;
			case INHERITANCE_SOLVED: {
				status = INTERFACE_SOLVED;
				result = get_analyzer()->resolve_interface();
			} break;
			case INTERFACE_SOLVED: {
				status = FULLY_SOLVED;
				result = get_analyzer()->resolve_body();
			} break;
			case FULLY_SOLVED: {
				return result;
			}
		}
	}

	return result;
}

Error GDScriptParserRef::raise_trait_status(TraitStatus p_new_status) {
	ERR_FAIL_COND_V(clearing, ERR_BUG);
	ERR_FAIL_COND_V(parser == nullptr && trait_status != TRAIT_EMPTY, ERR_BUG);

	if (p_new_status < trait_status) {
		return OK;
	}

	while (result == OK && p_new_status > trait_status) {
		switch (trait_status) {
			case TRAIT_EMPTY: {
				get_parser()->clear();
				trait_status = TRAIT_PARSED;
				String remapped_path = ResourceLoader::path_remap(path);
				String source = GDScriptCache::get_source_code(remapped_path);
				source_hash = source.hash();
				result = get_parser()->parse(source, path, false);
			} break;
			case TRAIT_PARSED: {
				trait_status = TRAIT_SOLVED;
				result = get_analyzer()->analyze();
			} break;
			case TRAIT_SOLVED: {
				return result;
			}
		}
	}

	return result;
}

void GDScriptParserRef::clear() {
	if (clearing) {
		return;
	}
	clearing = true;

	GDScriptParser *lparser = parser;
	GDScriptAnalyzer *lanalyzer = analyzer;

	parser = nullptr;
	analyzer = nullptr;
	status = EMPTY;
	trait_status = TRAIT_EMPTY;
	result = OK;
	source_hash = 0;

	clearing = false;

	if (lanalyzer != nullptr) {
		memdelete(lanalyzer);
	}

	if (lparser != nullptr) {
		memdelete(lparser);
	}
}

GDScriptParserRef::~GDScriptParserRef() {
	clear();

	if (!abandoned) {
		MutexLock lock(GDScriptCache::singleton->mutex);
		GDScriptCache::singleton->parser_map.erase(path);
	}
}

GDScriptCache *GDScriptCache::singleton = nullptr;

SafeBinaryMutex<GDScriptCache::BINARY_MUTEX_TAG> &_get_gdscript_cache_mutex() {
	return GDScriptCache::mutex;
}

template <>
thread_local SafeBinaryMutex<GDScriptCache::BINARY_MUTEX_TAG>::TLSData SafeBinaryMutex<GDScriptCache::BINARY_MUTEX_TAG>::tls_data(_get_gdscript_cache_mutex());
SafeBinaryMutex<GDScriptCache::BINARY_MUTEX_TAG> GDScriptCache::mutex;

void GDScriptCache::move_script(const String &p_from, const String &p_to) {
	if (singleton == nullptr || p_from == p_to || p_from.is_empty()) {
		return;
	}

	MutexLock lock(singleton->mutex);

	if (singleton->cleared) {
		return;
	}

	remove_parser(p_from);

	if (singleton->shallow_gdscript_cache.has(p_from) && !p_from.is_empty()) {
		singleton->shallow_gdscript_cache[p_to] = singleton->shallow_gdscript_cache[p_from];
	}
	singleton->shallow_gdscript_cache.erase(p_from);

	if (singleton->full_gdscript_cache.has(p_from) && !p_from.is_empty()) {
		singleton->full_gdscript_cache[p_to] = singleton->full_gdscript_cache[p_from];
	}
	singleton->full_gdscript_cache.erase(p_from);

	///trait/impl global registries key by path too, gotta follow the move or they go stale
	for (HashMap<StringName, String>::Iterator E = singleton->global_traits.begin(); E;) {
		HashMap<StringName, String>::Iterator next = E;
		++next;
		if (E->value == p_from) {
			singleton->global_traits[E->key] = p_to;
		}
		E = next;
	}
	for (HashMap<StringName, Vector<GlobalImplClaim>>::Iterator E = singleton->global_impls.begin(); E;) {
		HashMap<StringName, Vector<GlobalImplClaim>>::Iterator next = E;
		++next;
		for (int i = 0; i < E->value.size(); i++) {
			if (E->value[i].owning_path == p_from) {
				E->value.write[i].owning_path = p_to;
			}
		}
		E = next;
	}
}

void GDScriptCache::remove_script(const String &p_path) {
	if (singleton == nullptr) {
		return;
	}

	MutexLock lock(singleton->mutex);

	if (singleton->cleared) {
		return;
	}

	if (HashMap<String, Vector<ObjectID>>::Iterator E = singleton->abandoned_parser_map.find(p_path)) {
		for (ObjectID parser_ref_id : E->value) {
			Ref<GDScriptParserRef> parser_ref = { ObjectDB::get_instance(parser_ref_id) };
			if (parser_ref.is_valid()) {
				parser_ref->clear();
			}
		}
	}

	singleton->abandoned_parser_map.erase(p_path);

	if (singleton->parser_map.has(p_path)) {
		singleton->parser_map[p_path]->clear();
	}

	remove_parser(p_path);

	singleton->dependencies.erase(p_path);
	singleton->shallow_gdscript_cache.erase(p_path);
	singleton->full_gdscript_cache.erase(p_path);

	remove_global_trait_by_path(p_path);
	remove_global_impls_by_path(p_path);
}

///gdscript trait stuff

static void _scan_trait_scripts_in_dir(const String& p_dir) {
	Error err = OK;
	Ref<DirAccess> dir = DirAccess::open(p_dir, &err);
	if (err != OK || dir.is_null()) {
		return;
	}

	if (dir->file_exists(".gdignore")) {
		return;
	}

	dir->list_dir_begin();
	String file_name = dir->get_next();
	while (!file_name.is_empty()) {
		if (dir->current_is_dir()) {
			if (file_name != "." && file_name != ".." && file_name != "./") {
				_scan_trait_scripts_in_dir(p_dir.path_join(file_name));
			}
		} else if (file_name.ends_with(".gd")) {
			String script_path = p_dir.path_join(file_name);
			Ref<FileAccess> f = FileAccess::open(script_path, FileAccess::READ, &err);
			if (err == OK && f.is_valid()) {
				GDScriptParser parser;
				err = parser.parse(f->get_as_utf8_string(), script_path, false, false);
				if (err == OK && parser.is_trait_script()) {
					const GDScriptParser::TraitNode* trait = parser.get_trait_tree();
					if (trait != nullptr && trait->identifier != nullptr) {
						GDScriptCache::add_global_trait(trait->identifier->name, script_path);
					}
				}
			}
		}
		file_name = dir->get_next();
	}
}

void GDScriptCache::add_global_trait(const StringName& p_trait_name, const String& p_path) {
	MutexLock lock(singleton->mutex);
	singleton->global_traits[p_trait_name] = p_path;
}

void GDScriptCache::remove_global_trait(const StringName& p_trait_name) {
	MutexLock lock(singleton->mutex);
	singleton->global_traits.erase(p_trait_name);
	singleton->global_traits_project_scanned = false;
}

void GDScriptCache::remove_global_trait_by_path(const String& p_path) {
	MutexLock lock(singleton->mutex);
	for (HashMap<StringName, String>::Iterator E = singleton->global_traits.begin(); E;) {
		HashMap<StringName, String>::Iterator next = E;
		++next;
		if (E->value == p_path) {
			singleton->global_traits.remove(E);
			singleton->global_traits_project_scanned = false;
		}
		E = next;
	}
}

bool GDScriptCache::is_global_trait(const StringName& p_trait_name) {
	MutexLock lock(singleton->mutex);
	return singleton->global_traits.has(p_trait_name);
}

void GDScriptCache::get_global_trait_list(List<StringName>* r_traits) {
	MutexLock lock(singleton->mutex);
	for (const KeyValue<StringName, String>& E : singleton->global_traits) {
		r_traits->push_back(E.key);
	}
}

String GDScriptCache::get_global_trait_path(const StringName& p_trait_name) {
	{
		MutexLock lock(singleton->mutex);
		const String* path = singleton->global_traits.getptr(p_trait_name);
		if (path != nullptr) {
			return *path;
		}

		if (singleton->global_traits_project_scanned) {
			return String();
		}
	}

	///the fs normally populates this registry while the editor scans project
	///files, but runtime parsing can hit this lookup before that has happened in this
	///process. absolute cinema. do one lazy project scan on the first cache miss, 
	///then subsequent misses stay O(1) instead of repeatedly walking the whole project
	_scan_trait_scripts_in_dir("res://");

	MutexLock lock(singleton->mutex);
	singleton->global_traits_project_scanned = true;
	const String* path = singleton->global_traits.getptr(p_trait_name);
	return path != nullptr ? *path : String();
}

void GDScriptCache::add_global_impl_claims(const StringName& p_target_type_key, const StringName& p_trait_name, const Vector<StringName>& p_method_names, const String& p_path) {
	if (p_target_type_key == StringName()) {
		return; ///no key? more like no cross-file collision checks for this type kind! hah! hah...
				///...yeah, i should take breaks more often...
	}
	MutexLock lock(singleton->mutex);
	Vector<GlobalImplClaim>& claims = singleton->global_impls[p_target_type_key];
	for (const StringName& method_name : p_method_names) {
		GlobalImplClaim claim;
		claim.trait_name = p_trait_name;
		claim.method_name = method_name;
		claim.owning_path = p_path;
		claims.push_back(claim);
	}
}

void GDScriptCache::remove_global_impls_by_path(const String& p_path) {
	MutexLock lock(singleton->mutex);
	for (HashMap<StringName, Vector<GlobalImplClaim>>::Iterator E = singleton->global_impls.begin(); E;) {
		HashMap<StringName, Vector<GlobalImplClaim>>::Iterator next = E;
		++next;
		Vector<GlobalImplClaim>& claims = E->value;
		for (int i = claims.size() - 1; i >= 0; i--) {
			if (claims[i].owning_path == p_path) {
				claims.remove_at(i);
			}
		}
		if (claims.is_empty()) {
			singleton->global_impls.remove(E);
		}
		E = next;
	}
}

Vector<GDScriptCache::GlobalImplClaim> GDScriptCache::get_global_impl_claims(const StringName& p_target_type_key) {
	MutexLock lock(singleton->mutex);
	const Vector<GlobalImplClaim>* claims = singleton->global_impls.getptr(p_target_type_key);
	return claims != nullptr ? *claims : Vector<GlobalImplClaim>();
}

bool GDScriptCache::has_global_impl_method_claim(const StringName& p_target_type_key, const StringName& p_method_name) {
	{
		MutexLock lock(singleton->mutex);
		const Vector<GlobalImplClaim>* claims = singleton->global_impls.getptr(p_target_type_key);
		if (claims != nullptr) {
			for (const GlobalImplClaim& claim : *claims) {
				if (claim.method_name == p_method_name) {
					return true;
				}
			}
		}
	}

	ensure_global_impls_scanned();

	MutexLock lock(singleton->mutex);
	const Vector<GlobalImplClaim>* claims = singleton->global_impls.getptr(p_target_type_key);
	if (claims == nullptr) {
		return false;
	}
	for (const GlobalImplClaim& claim : *claims) {
		if (claim.method_name == p_method_name) {
			return true;
		}
	}
	return false;
}

void GDScriptCache::ensure_global_impls_scanned() {
	{
		MutexLock lock(singleton->mutex);
		if (singleton->global_impls_project_scanned || singleton->global_impls_project_scanning) {
			return;
		}
		singleton->global_impls_project_scanning = true;
	}

	_scan_trait_scripts_in_dir("res://");

	Vector<String> trait_paths;
	{
		MutexLock lock(singleton->mutex);
		for (const KeyValue<StringName, String>& E : singleton->global_traits) {
			trait_paths.push_back(E.value);
		}
	}

	for (const String& path : trait_paths) {
		Error err = OK;
		get_full_script(path, err);
	}

	MutexLock lock(singleton->mutex);
	singleton->global_impls_project_scanned = true;
	singleton->global_impls_project_scanning = false;
}

Ref<GDScriptParserRef> GDScriptCache::get_parser(const String &p_path, GDScriptParserRef::Status p_status, Error &r_error, const String &p_owner) {
	MutexLock lock(singleton->mutex);
	Ref<GDScriptParserRef> ref;
	if (!p_owner.is_empty() && p_path != p_owner) {
		singleton->dependencies[p_owner].insert(p_path);
		singleton->parser_inverse_dependencies[p_path].insert(p_owner);
	}
	if (singleton->parser_map.has(p_path)) {
		ref = Ref<GDScriptParserRef>(singleton->parser_map[p_path]);
		if (ref.is_null()) {
			r_error = ERR_INVALID_DATA;
			return ref;
		}
	} else {
		String remapped_path = ResourceLoader::path_remap(p_path);
		if (!FileAccess::exists(remapped_path)) {
			r_error = ERR_FILE_NOT_FOUND;
			return ref;
		}
		ref.instantiate();
		ref->path = p_path;
		singleton->parser_map[p_path] = ref.ptr();
	}
	r_error = ref->raise_status(p_status);

	return ref;
}

Ref<GDScriptTrait> GDScriptCache::get_cached_trait(const String &p_path, const StringName &p_trait_name, Error &r_error, const String &p_owner) {
	MutexLock lock(singleton->mutex);
	Ref<GDScriptParserRef> ref;
	if (!p_owner.is_empty() && p_path != p_owner) {
		singleton->dependencies[p_owner].insert(p_path);
		singleton->parser_inverse_dependencies[p_path].insert(p_owner);
	}
	if (singleton->parser_map.has(p_path)) {
		ref = Ref<GDScriptParserRef>(singleton->parser_map[p_path]);
		if (ref.is_null()) {
			r_error = ERR_INVALID_DATA;
			return Ref<GDScriptTrait>();
		}
	} else {
		String remapped_path = ResourceLoader::path_remap(p_path);
		if (!FileAccess::exists(remapped_path)) {
			r_error = ERR_FILE_NOT_FOUND;
			return Ref<GDScriptTrait>();
		}
		ref.instantiate();
		ref->path = p_path;
		singleton->parser_map[p_path] = ref.ptr();
	}

	r_error = ref->raise_trait_status(GDScriptParserRef::TRAIT_SOLVED);
	if (r_error != OK) {
		return Ref<GDScriptTrait>();
	}

	GDScriptParser* trait_parser = ref->get_parser();
	if (trait_parser == nullptr || !trait_parser->is_trait_script()) {
		r_error = ERR_INVALID_DATA;
		return Ref<GDScriptTrait>();
	}

	Ref<GDScriptTrait> found = ref->get_analyzer()->get_trait_analyzer()->get_local_trait(p_trait_name);
	if (found.is_null()) {
		r_error = ERR_DOES_NOT_EXIST;
	} else {
		///pin the parser ref so the AST nodes (FunctionNode*) stay alive
		///as long as this trait object does
		found->parser_ref = ref;
	}
	return found;
}

bool GDScriptCache::has_parser(const String &p_path) {
	MutexLock lock(singleton->mutex);
	return singleton->parser_map.has(p_path);
}

void GDScriptCache::remove_parser(const String &p_path) {
	MutexLock lock(singleton->mutex);

	if (singleton->parser_map.has(p_path)) {
		GDScriptParserRef *parser_ref = singleton->parser_map[p_path];
		parser_ref->abandoned = true;
		singleton->abandoned_parser_map[p_path].push_back(parser_ref->get_instance_id());
	}

	// Can't clear the parser because some other parser might be currently using it in the chain of calls.
	singleton->parser_map.erase(p_path);

	// Have to copy while iterating, because parser_inverse_dependencies is modified.
	HashSet<String> ideps(singleton->parser_inverse_dependencies[p_path]);
	singleton->parser_inverse_dependencies.erase(p_path);
	for (String idep_path : ideps) {
		remove_parser(idep_path);
	}
}

String GDScriptCache::get_source_code(const String &p_path) {
	Vector<uint8_t> source_file;
	Error err;
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ, &err);
	ERR_FAIL_COND_V(err, "");

	uint64_t len = f->get_length();
	source_file.resize(len + 1);
	uint64_t r = f->get_buffer(source_file.ptrw(), len);
	ERR_FAIL_COND_V(r != len, "");
	source_file.write[len] = 0;

	String source;
	if (source.append_utf8((const char *)source_file.ptr(), len) != OK) {
		ERR_FAIL_V_MSG("", "Script '" + p_path + "' contains invalid unicode (UTF-8), so it was not loaded. Please ensure that scripts are saved in valid UTF-8 unicode.");
	}
	return source;
}

Vector<uint8_t> GDScriptCache::get_binary_tokens(const String &p_path) {
	Vector<uint8_t> buffer;
	Error err = OK;
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ, &err);
	ERR_FAIL_COND_V_MSG(err != OK, buffer, "Failed to open binary GDScript file '" + p_path + "'.");

	uint64_t len = f->get_length();
	buffer.resize(len);
	uint64_t read = f->get_buffer(buffer.ptrw(), buffer.size());
	ERR_FAIL_COND_V_MSG(read != len, Vector<uint8_t>(), "Failed to read binary GDScript file '" + p_path + "'.");

	return buffer;
}

Ref<GDScript> GDScriptCache::get_shallow_script(const String &p_path, Error &r_error, const String &p_owner) {
	MutexLock lock(singleton->mutex);

	if (!p_owner.is_empty() && p_path != p_owner) {
		singleton->dependencies[p_owner].insert(p_path);
	}
	if (singleton->full_gdscript_cache.has(p_path)) {
		return singleton->full_gdscript_cache[p_path];
	}
	if (singleton->shallow_gdscript_cache.has(p_path)) {
		return singleton->shallow_gdscript_cache[p_path];
	}

	const String remapped_path = ResourceLoader::path_remap(p_path);

	Ref<GDScript> script;
	script.instantiate();

	script->set_path_cache(p_path);
	if (remapped_path.has_extension("gdc")) {
		Vector<uint8_t> buffer = get_binary_tokens(remapped_path);
		if (buffer.is_empty()) {
			r_error = ERR_FILE_CANT_READ;
		}
		script->set_binary_tokens_source(buffer);
	} else {
		r_error = script->load_source_code(remapped_path);
	}

	if (r_error) {
		return Ref<GDScript>(); // Returns null and does not cache when the script fails to load.
	}

	Ref<GDScriptParserRef> parser_ref = get_parser(p_path, GDScriptParserRef::PARSED, r_error);
	if (r_error == OK) {
		GDScriptCompiler::make_scripts(script.ptr(), parser_ref->get_parser()->get_tree(), true);
	}

	singleton->shallow_gdscript_cache[p_path] = script;

	return script;
}

Ref<GDScript> GDScriptCache::get_full_script(const String &p_path, Error &r_error, const String &p_owner, bool p_update_from_disk) {
	MutexLock lock(singleton->mutex);

	if (!p_owner.is_empty() && p_path != p_owner) {
		singleton->dependencies[p_owner].insert(p_path);
	}

	Ref<GDScript> script;
	r_error = OK;
	if (singleton->full_gdscript_cache.has(p_path)) {
		script = singleton->full_gdscript_cache[p_path];
		if (!p_update_from_disk) {
			return script;
		}
	}

	if (script.is_null()) {
		script = get_shallow_script(p_path, r_error);
		// Only exit early if script failed to load, otherwise let reload report errors.
		if (script.is_null()) {
			return script;
		}
	}

	const String remapped_path = ResourceLoader::path_remap(p_path);

	if (p_update_from_disk) {
		if (remapped_path.has_extension("gdc")) {
			Vector<uint8_t> buffer = get_binary_tokens(remapped_path);
			if (buffer.is_empty()) {
				r_error = ERR_FILE_CANT_READ;
				goto finish;
			}
			script->set_binary_tokens_source(buffer);
		} else {
			r_error = script->load_source_code(remapped_path);
			if (r_error) {
				goto finish;
			}
		}
	}

	// Allowing lifting the lock might cause a script to be reloaded multiple times,
	// which, as a last resort deadlock prevention strategy, is a good tradeoff.
	{
		uint32_t allowance_id = WorkerThreadPool::thread_enter_unlock_allowance_zone(singleton->mutex);
		r_error = script->reload(true);
		WorkerThreadPool::thread_exit_unlock_allowance_zone(allowance_id);
	}

finish:
	singleton->full_gdscript_cache[p_path] = script;
	singleton->shallow_gdscript_cache.erase(p_path);

	// Add the script to the resource cache. Usually ResourceLoader would take care of it, but cyclic references can break that sometimes so we do it ourselves.
	// Resources don't know whether they are cached, so using `set_path()` after `set_path_cache()` does not add the resource to the cache if the path is the same.
	// We reset the cached path from `get_shallow_script()` so that the subsequent call to `set_path()` caches everything correctly.
	script->set_path_cache(String());
	script->set_path(p_path, true);

	return script;
}

Ref<GDScript> GDScriptCache::get_cached_script(const String &p_path) {
	MutexLock lock(singleton->mutex);

	if (singleton->full_gdscript_cache.has(p_path)) {
		return singleton->full_gdscript_cache[p_path];
	}

	if (singleton->shallow_gdscript_cache.has(p_path)) {
		return singleton->shallow_gdscript_cache[p_path];
	}

	return Ref<GDScript>();
}

Error GDScriptCache::finish_compiling(const String &p_owner) {
	MutexLock lock(singleton->mutex);

	// Mark this as compiled.
	Ref<GDScript> script = get_cached_script(p_owner);
	singleton->full_gdscript_cache[p_owner] = script;
	singleton->shallow_gdscript_cache.erase(p_owner);

	HashSet<String> depends(singleton->dependencies[p_owner]);

	Error err = OK;
	for (const String &E : depends) {
		Error this_err = OK;
		// No need to save the script. We assume it's already referenced in the owner.
		get_full_script(E, this_err);

		if (this_err != OK) {
			err = this_err;
		}
	}

	singleton->dependencies.erase(p_owner);

	return err;
}

void GDScriptCache::add_static_script(Ref<GDScript> p_script) {
	ERR_FAIL_COND_MSG(p_script.is_null(), "Trying to cache empty script as static.");
	ERR_FAIL_COND_MSG(!p_script->is_script_valid(), "Trying to cache non-compiled script as static.");
	singleton->static_gdscript_cache[p_script->get_fully_qualified_name()] = p_script;
}

void GDScriptCache::remove_static_script(const String &p_fqcn) {
	singleton->static_gdscript_cache.erase(p_fqcn);
}

void GDScriptCache::clear() {
	if (singleton == nullptr) {
		return;
	}

	MutexLock lock(singleton->mutex);

	if (singleton->cleared) {
		return;
	}
	singleton->cleared = true;

	singleton->parser_inverse_dependencies.clear();

	for (const KeyValue<String, Vector<ObjectID>> &KV : singleton->abandoned_parser_map) {
		for (ObjectID parser_ref_id : KV.value) {
			Ref<GDScriptParserRef> parser_ref = { ObjectDB::get_instance(parser_ref_id) };
			if (parser_ref.is_valid()) {
				parser_ref->clear();
			}
		}
	}

	singleton->abandoned_parser_map.clear();

	RBSet<Ref<GDScriptParserRef>> parser_map_refs;
	for (KeyValue<String, GDScriptParserRef *> &E : singleton->parser_map) {
		parser_map_refs.insert(E.value);
	}

	singleton->parser_map.clear();

	for (Ref<GDScriptParserRef> &E : parser_map_refs) {
		if (E.is_valid()) {
			E->clear();
		}
	}

	parser_map_refs.clear();
	singleton->shallow_gdscript_cache.clear();
	singleton->full_gdscript_cache.clear();
	singleton->static_gdscript_cache.clear();
	singleton->global_traits.clear();
	singleton->global_traits_project_scanned = false;
	singleton->global_impls.clear();
	singleton->global_impls_project_scanned = false;
	singleton->global_impls_project_scanning = false;
}

GDScriptCache::GDScriptCache() {
	singleton = this;
}

GDScriptCache::~GDScriptCache() {
	if (!cleared) {
		clear();
	}
	singleton = nullptr;
}
