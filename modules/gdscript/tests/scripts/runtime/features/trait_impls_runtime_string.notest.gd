trait RuntimeTraitTestString

func rt_wrap(left: String = "[", right: String = "]") -> String {
	return left + self + right
}

func rt_shout() -> String {
	return self.to_upper() + "!"
}

impl for String {}
