# Third-Party Licenses

This project may optionally use third-party libraries depending on the chosen build configuration.

## pugixml (optional)
If `pugixml` is included in the build:
- License: MIT
- Website: https://pugixml.org/
- Repository: https://github.com/zeux/pugixml

No action is required when pugixml is provided by the system (e.g., via package manager or shared library).


## utf8proc (optional, only if `USE_UTF8PROC` is defined)
If compiled with `-DUSE_UTF8PROC`:
- Unicode normalization and case-folding routines are provided by the `utf8proc` library
- License: MIT
- Repository: https://github.com/JuliaStrings/utf8proc

If `USE_UTF8PROC` is not defined:
- No code from utf8proc is linked or distributed


## Summary

| Library   | Used When                         | Must Include License Text? |
|----------|-----------------------------------|---------------------------|
| pugixml   | When statically linked or bundled | Yes                       |
| utf8proc  | Only when `USE_UTF8PROC` is set   | Yes                       |
| none      | When system-provided dependencies | No                        |

This project itself is licensed under the MIT License.
