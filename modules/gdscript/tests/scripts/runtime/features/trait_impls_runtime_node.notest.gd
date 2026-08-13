trait RuntimeTraitTestNode

func rt_named(prefix: String = "node") -> String {
	return prefix + ":" + self.name
}

impl for Node {}
