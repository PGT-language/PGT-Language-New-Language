#pragma once
#include <string>
#include <vector>

namespace pgt {

struct TemplateFile {
    std::string path;
    std::string content;
};

struct ProjectTemplate {
    std::string name;
    std::string description;
    std::vector<TemplateFile> files;
};

const std::vector<ProjectTemplate>& projectTemplates();

const ProjectTemplate* findTemplate(const std::string& name);

}
