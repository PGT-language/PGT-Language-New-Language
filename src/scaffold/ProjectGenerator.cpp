#include "scaffold/ProjectGenerator.h"

#include "scaffold/Templates.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace pgt {
namespace {

namespace fs = std::filesystem;

bool validName(const std::string& name) {
    if (name.empty() || name[0] == '-') return false;
    for (char c : name) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

std::string substitute(std::string text, const std::string& module) {
    const std::string key = "{{module}}";
    for (size_t pos = text.find(key); pos != std::string::npos; pos = text.find(key, pos)) {
        text.replace(pos, key.size(), module);
        pos += module.size();
    }
    return text;
}

void writeFile(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write '" + path.string() + "'");
    out << content;
}

}

void createProject(const std::string& name, const std::string& templateName) {
    if (!validName(name))
        throw std::runtime_error("invalid project name '" + name +
                                 "': use letters, digits, '_' and '-'");

    const ProjectTemplate* tpl = findTemplate(templateName);
    if (!tpl) {
        std::string known;
        for (const ProjectTemplate& t : projectTemplates())
            known += (known.empty() ? "" : ", ") + t.name;
        throw std::runtime_error("unknown template '" + templateName + "' (available: " + known +
                                 ")");
    }

    fs::path dir(name);
    if (fs::exists(dir)) throw std::runtime_error("'" + name + "' already exists");

    writeFile(dir / "pgt.mod", "module " + name + "\n\nversion 0.1\n\ndependencies {\n}\n");
    for (const TemplateFile& f : tpl->files)
        writeFile(dir / f.path, substitute(f.content, name));
}

}
