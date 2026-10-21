# CMake script: generate skills_generated.h from skills/bin/*.md
#
# Invoked via: include(cmake/gen_skills.cmake)
#
# Produces: ${CMAKE_BINARY_DIR}/skills_generated.h

file(GLOB HALI_SKILL_FILES ${CMAKE_SOURCE_DIR}/skills/bin/*.md)
list(SORT HALI_SKILL_FILES)

set(SKILL_ENTRIES "")

foreach(skill_file ${HALI_SKILL_FILES})
  # ── name: filename stem, lowercase, hyphens → underscores ──
  get_filename_component(skill_name ${skill_file} NAME_WE)
  string(TOLOWER "${skill_name}" skill_name)
  string(REPLACE "-" "_" skill_name "${skill_name}")

  # ── read file content ──
  file(READ ${skill_file} skill_content)

  # ── summary: first non-empty, non-heading line after the title ──
  string(REPLACE "\n" ";" skill_lines "${skill_content}")
  # Remove the first line (the # Title heading)
  list(REMOVE_AT skill_lines 0)
  set(skill_summary "")
  foreach(line ${skill_lines})
    if(NOT line MATCHES "^#" AND NOT line MATCHES "^[ \t]*$")
      set(skill_summary "${line}")
      break()
    endif()
  endforeach()
  # Trim leading/trailing whitespace from summary
  string(STRIP skill_summary skill_summary)
  # Truncate to 55 chars
  string(LENGTH skill_summary summary_len)
  if(summary_len GREATER 55)
    string(SUBSTRING skill_summary 0 55 skill_summary)
    string(APPEND skill_summary "…")
  endif()

  # ── escape content as a C string literal ──
  set(escaped "${skill_content}")
  # 1. Escape backslashes
  string(REPLACE "\\" "\\\\" escaped "${escaped}")
  # 2. Escape double quotes
  string(REPLACE "\"" "\\\"" escaped "${escaped}")
  # 3. Replace newlines with \n
  string(REPLACE "\n" "\\n" escaped "${escaped}")

  # ── append entry ──
  set(SKILL_ENTRIES "${SKILL_ENTRIES}  {\"${skill_name}\",\n   \"${skill_summary}\",\n   \"${escaped}\"},\n")
endforeach()
list(LENGTH HALI_SKILL_FILES SKILL_COUNT)

# ── write generated header ──
set(SKILLS_HEADER "#ifndef SKILLS_GENERATED_H\n")
string(APPEND SKILLS_HEADER "#define SKILLS_GENERATED_H\n\n")
string(APPEND SKILLS_HEADER "#include <string>\n")
string(APPEND SKILLS_HEADER "#include <vector>\n\n")
string(APPEND SKILLS_HEADER "struct SkillEntry {\n")
string(APPEND SKILLS_HEADER "  const char *name;\n")
string(APPEND SKILLS_HEADER "  const char *summary;\n")
string(APPEND SKILLS_HEADER "  const char *text;\n")
string(APPEND SKILLS_HEADER "};\n\n")
string(APPEND SKILLS_HEADER "static const SkillEntry SKILLS[] = {\n")
string(APPEND SKILLS_HEADER "${SKILL_ENTRIES}")
string(APPEND SKILLS_HEADER "};\n\n")
string(APPEND SKILLS_HEADER "static const int SKILL_COUNT = ${SKILL_COUNT};\n\n")
string(APPEND SKILLS_HEADER "inline const SkillEntry *skill_lookup(const std::string &name) {\n")
string(APPEND SKILLS_HEADER "  for (int i = 0; i < SKILL_COUNT; i++) {\n")
string(APPEND SKILLS_HEADER "    if (name == SKILLS[i].name) return &SKILLS[i];\n")
string(APPEND SKILLS_HEADER "  }\n")
string(APPEND SKILLS_HEADER "  return nullptr;\n")
string(APPEND SKILLS_HEADER "}\n\n")
string(APPEND SKILLS_HEADER "inline std::vector<std::string> skill_display_names() {\n")
string(APPEND SKILLS_HEADER "  std::vector<std::string> v;\n")
string(APPEND SKILLS_HEADER "  v.reserve(SKILL_COUNT);\n")
string(APPEND SKILLS_HEADER "  for (int i = 0; i < SKILL_COUNT; i++) {\n")
string(APPEND SKILLS_HEADER "    v.push_back(std::string(SKILLS[i].name) + \" — \" + SKILLS[i].summary);\n")
string(APPEND SKILLS_HEADER "  }\n")
string(APPEND SKILLS_HEADER "  return v;\n")
string(APPEND SKILLS_HEADER "}\n\n")
string(APPEND SKILLS_HEADER "#endif\n")

file(WRITE ${CMAKE_BINARY_DIR}/skills_generated.h "${SKILLS_HEADER}")
message(STATUS "Generated skills_generated.h with ${SKILL_COUNT} skills")
