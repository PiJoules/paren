import lit.formats

from pathlib import Path

config.name = "Paren tests"
config.test_format = lit.formats.ShTest(True)
config.suffixes = [".par"]
config.test_source_root = Path(config.src_root) / "tests"
config.test_exec_root = Path(config.build_root) / "tests"

build_root = Path(config.build_root)
paren_path = build_root / "paren"
library_paren_path = build_root / "library.paren"

config.substitutions.append(("%paren", f"{paren_path} -i {library_paren_path}"))
config.substitutions.append(
    (
        "%cxx",
        f"{config.cxx} -fsanitize=address -Wl,--whole-archive -lparen -L{config.build_root} -Wl,--no-whole-archive",
    )
)
config.substitutions.append(("FileCheck", config.filecheck))
