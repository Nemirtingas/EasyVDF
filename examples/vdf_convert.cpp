// Converts a VDF file between the text, binary and VBKV formats.
//
// Usage: vdf_convert <input> <output> [text|binary|vbkv]
//   vdf_convert shortcuts.vdf shortcuts.txt              (binary -> text, the default)
//   vdf_convert shortcuts.txt shortcuts.vdf binary
//
// The input format is detected. A file holding several root objects (for example a
// signed installscript.vdf) is converted root by root, in text only.

#include <EasyVDF.h>

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        std::cerr << "usage: " << argv[0] << " <input> <output> [text|binary|vbkv]\n";
        return 2;
    }

    const std::string format_name = argc > 3 ? argv[3] : "text";
    EasyVDF::Format format;
    if (format_name == "text")
        format = EasyVDF::Format::Text;
    else if (format_name == "binary")
        format = EasyVDF::Format::Binary;
    else if (format_name == "vbkv")
        format = EasyVDF::Format::Vbkv;
    else
    {
        std::cerr << "unknown format '" << format_name << "', expected text, binary or vbkv\n";
        return 2;
    }

    const EasyVDF::Result<std::vector<EasyVDF::KeyValue>> roots = EasyVDF::load_all(argv[1]);
    if (!roots)
    {
        // line and column are 0 for binary input and I/O errors.
        const EasyVDF::ErrorInfo& error = roots.error();
        std::cerr << argv[1];
        if (error.line != 0)
            std::cerr << ':' << error.line << ':' << error.column;
        std::cerr << ": " << error.message << '\n';
        return 1;
    }

    if (roots->size() == 1)
    {
        const EasyVDF::Result<void> saved = roots->front().save(argv[2], format);
        if (!saved)
        {
            std::cerr << saved.error().message << '\n';
            return 1;
        }
    }
    else if (format == EasyVDF::Format::Text)
    {
        std::ofstream out(argv[2], std::ios::binary);
        for (const auto& root : *roots)
            out << root;
        if (!out)
        {
            std::cerr << "can't write " << argv[2] << '\n';
            return 1;
        }
    }
    else
    {
        std::cerr << argv[1] << " holds " << roots->size() << " root objects, only text can store several of them\n";
        return 1;
    }

    std::cout << argv[1] << " -> " << argv[2] << " (" << format_name << ", " << roots->size() << " root object"
              << (roots->size() > 1 ? "s" : "") << ")\n";
    return 0;
}
