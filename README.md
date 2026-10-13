 ini.h - INI loader/parser
 
# Features

- Key/value pairs using strings and numbers. (e.g. `WINDOW_WIDTH = 720`)
- Commented lines using `#`. (e.g. `#this is a comment`)

# Usage

- Define `INI_IMPLEMENTATION` in one (and only one) translation unit right before including the `ini.h` header. This will allow the preprocessor to append the libraries function definitions for compilation.
- Use `ini_load` to load/parse an INI file given a designated file path.
- Use `ini_get` to retrieve values for a given key.

