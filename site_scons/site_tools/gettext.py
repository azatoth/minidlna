# vi: syntax=python:et:ts=4
from SCons.Builder import Builder
from SCons.Action  import Action
from SCons.Util    import WhereIs

def exists():
    return True

def generate(env):
    env["MSGFMT"] = WhereIs("msgfmt")
    action = [[
        "$MSGFMT",
        "--check-format",
        "--check-domain",
        "-f",
        "-o", "$TARGET",
        "$SOURCE"
    ]]
    env["GETTEXTSTR"] = action

    env["BUILDERS"]["Gettext"] = Builder(
        action = Action( action, "$GETTEXTSTR" ),
        src_suffix = ".po",
        suffix = ".mo",
        single_source = True
    )
