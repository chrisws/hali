// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

#include "pom.h"

namespace {

// Read entire file into a string.
std::string read_file(const std::string &path) {
  std::ifstream f(path);
  if (!f) {
    return "";
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// Write string to file (overwrite).
bool write_file(const std::string &path, const std::string &content) {
  std::ofstream f(path);
  if (!f) {
    return false;
  }
  f << content;
  return true;
}

// Trim leading/trailing whitespace.
std::string trim(const std::string &s) {
  size_t start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return "";
  }
  size_t end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

// Extract text between <tag> and </tag> from a string.
// Returns empty string if not found.
std::string extract_tag(const std::string &content, const std::string &tag) {
  std::string open = "<" + tag + ">";
  std::string close = "</" + tag + ">";
  size_t start = content.find(open);
  if (start == std::string::npos) {
    return "";
  }
  start += open.size();
  size_t end = content.find(close, start);
  if (end == std::string::npos) {
    return "";
  }
  return trim(content.substr(start, end - start));
}

// Replace the content of <tag>...</tag> with new_content.
// Returns true if replacement was made.
bool replace_tag_content(std::string &content, const std::string &tag,
                         const std::string &new_content) {
  std::string open = "<" + tag + ">";
  std::string close = "</" + tag + ">";
  size_t start = content.find(open);
  if (start == std::string::npos) {
    return false;
  }
  start += open.size();
  size_t end = content.find(close, start);
  if (end == std::string::npos) {
    return false;
  }
  content.replace(start, end - start, new_content);
  return true;
}

// Check if a dependency with given groupId+artifactId already exists.
bool has_dependency(const std::string &content, const std::string &gid,
                    const std::string &aid) {
  std::string dep_block = "<dependency>";
  size_t pos = 0;
  while (pos < content.size()) {
    size_t start = content.find(dep_block, pos);
    if (start == std::string::npos) {
      break;
    }
    size_t end = content.find("</dependency>", start);
    if (end == std::string::npos) {
      break;
    }
    std::string block = content.substr(start, end - start + 13);
    if (block.find("<groupId>" + gid + "</groupId>") != std::string::npos &&
        block.find("<artifactId>" + aid + "</artifactId>") != std::string::npos) {
      return true;
    }
    pos = end + 1;
  }
  return false;
}

// Insert a <dependencies> section before </project> if it doesn't exist.
void ensure_dependencies_section(std::string &content) {
  if (content.find("<dependencies>") != std::string::npos) {
    return;
  }
  std::string section =
      "  <dependencies>\n"
      "  </dependencies>\n";
  size_t pos = content.find("</project>");
  if (pos != std::string::npos) {
    content.insert(pos, section);
  }
}

// Insert a <properties> section after <packaging> or after <version> if not present.
void ensure_properties_section(std::string &content) {
  if (content.find("<properties>") != std::string::npos) {
    return;
  }
  std::string section =
      "  <properties>\n"
      "  </properties>\n";
  // Insert after <packaging>...</packaging> if present, else after <version>...</version>
  size_t pos = content.find("</packaging>");
  if (pos == std::string::npos) {
    pos = content.find("</version>");
  }
  if (pos != std::string::npos) {
    pos += 12; // length of "</packaging>"
    // Find end of line
    size_t eol = content.find('\n', pos);
    if (eol != std::string::npos) {
      pos = eol + 1;
    }
    content.insert(pos, section);
  }
}

// Insert <build><plugins> section before </project> if not present.
void ensure_build_plugins_section(std::string &content) {
  if (content.find("<plugins>") != std::string::npos) {
    return;
  }
  std::string section =
      "  <build>\n"
      "    <plugins>\n"
      "    </plugins>\n"
      "  </build>\n";
  size_t pos = content.find("</project>");
  if (pos != std::string::npos) {
    content.insert(pos, section);
  }
}

//
// show: display structured pom information
//
PomResult pom_show(const std::string &pom_path) {
  std::string content = read_file(pom_path);
  if (content.empty()) {
    return {false, "ERROR: cannot read " + pom_path};
  }

  std::string out;
  out += "==> POM: " + pom_path + "\n";
  out += "  groupId:    " + extract_tag(content, "groupId") + "\n";
  out += "  artifactId: " + extract_tag(content, "artifactId") + "\n";
  out += "  version:    " + extract_tag(content, "version") + "\n";
  out += "  packaging:  " + extract_tag(content, "packaging") + "\n";

  // Properties
  std::string props = extract_tag(content, "properties");
  if (!props.empty()) {
    out += "  properties:\n";
    std::istringstream ps(props);
    std::string line;
    while (std::getline(ps, line)) {
      line = trim(line);
      if (line.empty() || line == "<properties>" || line == "</properties>") {
        continue;
      }
      // Extract tag name and value
      size_t open = line.find('<');
      size_t close = line.find('>');
      if (open != std::string::npos && close != std::string::npos) {
        std::string tag = line.substr(open + 1, close - open - 1);
        size_t val_start = line.find('>', close) + 1;
        size_t val_end = line.rfind('<');
        if (val_start != std::string::npos && val_end != std::string::npos &&
            val_end > val_start) {
          out += "    " + tag + " = " + line.substr(val_start, val_end - val_start) + "\n";
        }
      }
    }
  }

  // Dependencies
  std::string deps = extract_tag(content, "dependencies");
  if (!deps.empty()) {
    out += "  dependencies:\n";
    size_t pos = 0;
    while (pos < deps.size()) {
      size_t start = deps.find("<dependency>", pos);
      if (start == std::string::npos) {
        break;
      }
      size_t end = deps.find("</dependency>", start);
      if (end == std::string::npos) {
        break;
      }
      std::string block = deps.substr(start, end - start + 13);
      std::string gid = extract_tag(block, "groupId");
      std::string aid = extract_tag(block, "artifactId");
      std::string ver = extract_tag(block, "version");
      std::string scope = extract_tag(block, "scope");
      out += "    " + gid + ":" + aid + ":" + ver;
      if (!scope.empty()) {
        out += " (" + scope + ")";
      }
      out += "\n";
      pos = end + 1;
    }
  } else {
    out += "  dependencies: (none)\n";
  }

  // Plugins
  std::string plugins = extract_tag(content, "plugins");
  if (!plugins.empty()) {
    out += "  plugins:\n";
    size_t pos = 0;
    while (pos < plugins.size()) {
      size_t start = plugins.find("<plugin>", pos);
      if (start == std::string::npos) {
        break;
      }
      size_t end = plugins.find("</plugin>", start);
      if (end == std::string::npos) {
        break;
      }
      std::string block = plugins.substr(start, end - start + 9);
      std::string gid = extract_tag(block, "groupId");
      std::string aid = extract_tag(block, "artifactId");
      std::string ver = extract_tag(block, "version");
      out += "    " + gid + ":" + aid + ":" + ver + "\n";
      pos = end + 1;
    }
  } else {
    out += "  plugins: (none)\n";
  }

  return {true, out};
}

//
// add-dep: add a dependency
// args: groupId:artifactId:version[:scope]
//
PomResult pom_add_dep(const std::string &pom_path, const std::string &args) {
  // Parse args
  std::vector<std::string> parts;
  std::istringstream ss(args);
  std::string token;
  while (std::getline(ss, token, ':')) {
    parts.push_back(token);
  }
  if (parts.size() < 3) {
    return {false, "ERROR: add-dep requires groupId:artifactId:version[:scope]"};
  }
  std::string gid = parts[0];
  std::string aid = parts[1];
  std::string ver = parts[2];
  std::string scope = (parts.size() >= 4) ? parts[3] : "compile";

  std::string content = read_file(pom_path);
  if (content.empty()) {
    return {false, "ERROR: cannot read " + pom_path};
  }

  if (has_dependency(content, gid, aid)) {
    return {false, "ERROR: dependency " + gid + ":" + aid + " already exists"};
  }

  ensure_dependencies_section(content);

  std::string dep =
      "    <dependency>\n"
      "      <groupId>" + gid + "</groupId>\n"
      "      <artifactId>" + aid + "</artifactId>\n"
      "      <version>" + ver + "</version>\n"
      "      <scope>" + scope + "</scope>\n"
      "    </dependency>\n";

  // Insert before </dependencies>
  size_t pos = content.find("</dependencies>");
  if (pos != std::string::npos) {
    content.insert(pos, dep);
  }

  if (!write_file(pom_path, content)) {
    return {false, "ERROR: cannot write " + pom_path};
  }

  return {true, "==> Added dependency: " + gid + ":" + aid + ":" + ver +
              " (" + scope + ")\n"};
}

//
// remove-dep: remove a dependency
// args: groupId:artifactId
//
PomResult pom_remove_dep(const std::string &pom_path, const std::string &args) {
  std::vector<std::string> parts;
  std::istringstream ss(args);
  std::string token;
  while (std::getline(ss, token, ':')) {
    parts.push_back(token);
  }
  if (parts.size() < 2) {
    return {false, "ERROR: remove-dep requires groupId:artifactId"};
  }
  std::string gid = parts[0];
  std::string aid = parts[1];

  std::string content = read_file(pom_path);
  if (content.empty()) {
    return {false, "ERROR: cannot read " + pom_path};
  }

  if (!has_dependency(content, gid, aid)) {
    return {false, "ERROR: dependency " + gid + ":" + aid + " not found"};
  }

  // Find and remove the dependency block
  size_t pos = 0;
  while (pos < content.size()) {
    size_t start = content.find("<dependency>", pos);
    if (start == std::string::npos) {
      break;
    }
    size_t end = content.find("</dependency>", start);
    if (end == std::string::npos) {
      break;
    }
    end += 13; // include </dependency>
    std::string block = content.substr(start, end - start);
    if (block.find("<groupId>" + gid + "</groupId>") != std::string::npos &&
        block.find("<artifactId>" + aid + "</artifactId>") != std::string::npos) {
      // Remove the block including trailing newline
      size_t remove_end = end;
      if (remove_end < content.size() && content[remove_end] == '\n') {
        remove_end++;
      }
      content.erase(start, remove_end - start);
      break;
    }
    pos = end;
  }

  if (!write_file(pom_path, content)) {
    return {false, "ERROR: cannot write " + pom_path};
  }

  return {true, "==> Removed dependency: " + gid + ":" + aid + "\n"};
}

//
// set-property: set a property value
// args: name value
//
PomResult pom_set_property(const std::string &pom_path, const std::string &args) {
  // Split on first space
  size_t space = args.find(' ');
  if (space == std::string::npos) {
    return {false, "ERROR: set-property requires <name> <value>"};
  }
  std::string name = args.substr(0, space);
  std::string value = trim(args.substr(space + 1));

  std::string content = read_file(pom_path);
  if (content.empty()) {
    return {false, "ERROR: cannot read " + pom_path};
  }

  ensure_properties_section(content);

  // Check if property already exists
  std::string tag = "<" + name + ">";
  std::string close_tag = "</" + name + ">";
  size_t pos = content.find(tag);
  if (pos != std::string::npos) {
    pos += tag.size();
    size_t end = content.find(close_tag, pos);
    if (end != std::string::npos) {
      content.replace(pos, end - pos, value);
    }
  } else {
    // Insert before </properties>
    size_t insert_pos = content.find("</properties>");
    if (insert_pos != std::string::npos) {
      content.insert(insert_pos, "    " + name + " " + value + " " + close_tag + "\n");
    }
  }

  if (!write_file(pom_path, content)) {
    return {false, "ERROR: cannot write " + pom_path};
  }

  return {true, "==> Set property: " + name + " = " + value + "\n"};
}

//
// set-version: set project version
//
PomResult pom_set_version(const std::string &pom_path, const std::string &args) {
  std::string content = read_file(pom_path);
  if (content.empty()) {
    return {false, "ERROR: cannot read " + pom_path};
  }

  if (!replace_tag_content(content, "version", args)) {
    return {false, "ERROR: could not find <version> tag in pom.xml"};
  }

  if (!write_file(pom_path, content)) {
    return {false, "ERROR: cannot write " + pom_path};
  }

  return {true, "==> Set version: " + args + "\n"};
}

//
// set-group-id: set groupId
//
PomResult pom_set_group_id(const std::string &pom_path, const std::string &args) {
  std::string content = read_file(pom_path);
  if (content.empty()) {
    return {false, "ERROR: cannot read " + pom_path};
  }

  if (!replace_tag_content(content, "groupId", args)) {
    return {false, "ERROR: could not find <groupId> tag in pom.xml"};
  }

  if (!write_file(pom_path, content)) {
    return {false, "ERROR: cannot write " + pom_path};
  }

  return {true, "==> Set groupId: " + args + "\n"};
}

//
// set-artifact-id: set artifactId
//
PomResult pom_set_artifact_id(const std::string &pom_path, const std::string &args) {
  std::string content = read_file(pom_path);
  if (content.empty()) {
    return {false, "ERROR: cannot read " + pom_path};
  }

  if (!replace_tag_content(content, "artifactId", args)) {
    return {false, "ERROR: could not find <artifactId> tag in pom.xml"};
  }

  if (!write_file(pom_path, content)) {
    return {false, "ERROR: cannot write " + pom_path};
  }

  return {true, "==> Set artifactId: " + args + "\n"};
}

//
// add-plugin: add a build plugin
// args: groupId:artifactId:version
//
PomResult pom_add_plugin(const std::string &pom_path, const std::string &args) {
  std::vector<std::string> parts;
  std::istringstream ss(args);
  std::string token;
  while (std::getline(ss, token, ':')) {
    parts.push_back(token);
  }
  if (parts.size() < 3) {
    return {false, "ERROR: add-plugin requires groupId:artifactId:version"};
  }
  std::string gid = parts[0];
  std::string aid = parts[1];
  std::string ver = parts[2];

  std::string content = read_file(pom_path);
  if (content.empty()) {
    return {false, "ERROR: cannot read " + pom_path};
  }

  ensure_build_plugins_section(content);

  std::string plugin =
      "      <plugin>\n"
      "        <groupId>" + gid + "</groupId>\n"
      "        <artifactId>" + aid + "</artifactId>\n"
      "        <version>" + ver + "</version>\n"
      "      </plugin>\n";

  size_t pos = content.find("</plugins>");
  if (pos != std::string::npos) {
    content.insert(pos, plugin);
  }

  if (!write_file(pom_path, content)) {
    return {false, "ERROR: cannot write " + pom_path};
  }

  return {true, "==> Added plugin: " + gid + ":" + aid + ":" + ver + "\n"};
}

}  // namespace

PomResult pom_run(const std::string &pom_path, const std::string &op,
                  const std::function<void(const std::string &)> &status) {
  status("pom: " + op);

  // Split op into command and args
  std::string cmd;
  std::string args;
  size_t space = op.find(' ');
  if (space == std::string::npos) {
    cmd = op;
  } else {
    cmd = op.substr(0, space);
    args = trim(op.substr(space + 1));
  }

  if (cmd == "show") {
    return pom_show(pom_path);
  }
  if (cmd == "add-dep") {
    return pom_add_dep(pom_path, args);
  }
  if (cmd == "remove-dep") {
    return pom_remove_dep(pom_path, args);
  }
  if (cmd == "set-property") {
    return pom_set_property(pom_path, args);
  }
  if (cmd == "set-version") {
    return pom_set_version(pom_path, args);
  }
  if (cmd == "set-group-id") {
    return pom_set_group_id(pom_path, args);
  }
  if (cmd == "set-artifact-id") {
    return pom_set_artifact_id(pom_path, args);
  }
  if (cmd == "add-plugin") {
    return pom_add_plugin(pom_path, args);
  }

  return {false, "ERROR: unknown pom operation: " + cmd +
                  "\nAvailable: show, add-dep, remove-dep, set-property, "
                  "set-version, set-group-id, set-artifact-id, add-plugin"};
}