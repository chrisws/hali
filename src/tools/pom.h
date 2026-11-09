// This file is part of Hali
//
// Copyright(C) 2026 Chris Warren-Smith.
//
// This program is distributed under the terms of the GPL v2.0
// Download the GNU Public License (GPL) from www.gnu.org
//

#ifndef POM_H
#define POM_H

#include <functional>
#include <string>

struct PomResult {
  bool success_;
  std::string output_;
};

// pom_run: dispatch pom operations on a Maven pom.xml file.
//
// op format:
//   show
//   add-dep <groupId>:<artifactId>:<version>[:<scope>]
//   remove-dep <groupId>:<artifactId>
//   set-property <name> <value>
//   set-version <version>
//   set-group-id <groupId>
//   set-artifact-id <artifactId>
//   add-plugin <groupId>:<artifactId>:<version>
//
// pom_path: full path to pom.xml
// status: progress callback
PomResult pom_run(const std::string &pom_path, const std::string &op,
                  const std::function<void(const std::string &)> &status);

#endif