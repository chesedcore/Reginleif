/**************************************************************************/
/*  gdscript_trait_analyzer.cpp                                           */
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

#include "gdscript_trait_analyzer.h"
#include "gdscript_analyzer.h"

GDScriptTraitAnalyzer::GDScriptTraitAnalyzer(GDScriptParser* p_parser, GDScriptAnalyzer* p_analyzer) {
	parser = p_parser;
	analyzer = p_analyzer;
}

void GDScriptTraitAnalyzer::push_error(const String& p_message, const GDScriptParser::Node* p_source) {
	parser->push_error(p_message, p_source);
}

Error GDScriptTraitAnalyzer::resolve_trait(GDScriptParser::TraitNode* p_trait) {
	return OK;
}

Error GDScriptTraitAnalyzer::resolve_impl(GDScriptParser::ImplNode* p_impl) {
	return OK;
}

Error GDScriptTraitAnalyzer::check_trait_satisfaction(GDScriptParser::ClassNode* p_class) {
	return OK;
}

bool GDScriptTraitAnalyzer::check_orphan_rule(const Ref<GDScriptTrait>& p_trait, const GDScriptParser::DataType& p_target_type, const GDScriptParser::Node* p_source) {
	return true;
}

Ref<GDScriptTrait> GDScriptTraitAnalyzer::get_local_trait(const StringName& p_name) const {
	if (resolved_traits.has(p_name)) {
		return resolved_traits[p_name];
	}
	return Ref<GDScriptTrait>();
}

bool GDScriptTraitAnalyzer::type_satisfies_trait(const GDScriptParser::DataType& p_concrete_type, const Ref<GDScriptTrait>& p_trait) const {
	return false;
}