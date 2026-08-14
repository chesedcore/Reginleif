import os
from SCons.Variables import BoolVariable

def can_build(env, platform):
    return True

def get_opts(platform):
    return [
        BoolVariable("solar_brace_release", "Build Solar Brace in release mode", "no"),
    ]

def configure(env):
    rust_dir = os.path.join(os.path.dirname(__file__), "solar_brace")
    profile = "release" if env.get("solar_brace_release", "no") == "yes" else "debug"
    lib_dir = os.path.join(rust_dir, "target", profile)
    cargo_toml = os.path.join(rust_dir, "Cargo.toml")

    build_cmd = f"cargo build{' --release' if profile == 'release' else ''}"

    env.Command(
        target=os.path.join(lib_dir, "solar_brace.lib"),
        source=cargo_toml,
        action=f"cd {rust_dir} && {build_cmd}",
    )

    env.Append(LIBPATH=[lib_dir])
    if env["platform"] == "windows":
        env.Append(LINKFLAGS=[f"/LIBPATH:{lib_dir}", "solar_brace.lib"])
        env.Append(LINKFLAGS=["/FORCE:MULTIPLE"])
    else:
        env.Append(LIBS=["solar_brace"])