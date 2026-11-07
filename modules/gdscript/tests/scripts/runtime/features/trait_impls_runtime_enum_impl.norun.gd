trait RuntimeTraitTestEnumBool

func rt_bool_string() -> String

impl for TraitRuntimeEnumNode.Bool {
	func rt_bool_string() -> String {
		match self:
			TraitRuntimeEnumNode.Bool.TRUE:
				return "true"
			TraitRuntimeEnumNode.Bool.FALSE:
				return "false"
			_:
				assert(false)
				return ""
	}
}
