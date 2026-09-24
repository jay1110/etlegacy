#!/usr/bin/env node

import { mkdir, readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import process from "node:process";
import { fileURLToPath } from "node:url";

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, "../..");
const outputPath = path.resolve(process.cwd(), process.argv[2] ?? "THIRD_PARTY_NOTICES.txt");

const components = [
  { name: "cJSON", version: "1.7.15", files: ["libs/cjson/LICENSE"] },
  {
    name: "libjpeg-turbo",
    version: "2.0.4",
    files: ["libs/jpegturbo/LICENSE.md", "libs/jpegturbo/README.ijg"],
  },
  { name: "libpng", version: "1.6.47", files: ["libs/libpng/LICENSE"] },
  { name: "MiniZip", version: "1.1", files: ["libs/minizip/unzip.h"] },
  { name: "zlib", version: "1.3.1", files: ["libs/zlib/LICENSE"] },
  {
    name: "gl4es",
    version: "17f0894e19d1553e4176276c759915dab44c08e2",
    files: ["compliance/licenses/gl4es-LICENSE"],
  },
  {
    name: "SDL2 Emscripten port",
    version: "2.32.8 (Emscripten 4.0.23)",
    files: ["compliance/licenses/SDL2-LICENSE.txt"],
  },
  { name: "findlocale", version: "repository snapshot", files: ["libs/findlocale/LICENSE"] },
  {
    name: "Paul E. Jones SHA-1 implementation",
    version: "1998 snapshot",
    files: ["vendor/sha-1/license.txt"],
  },
];

const separator = "=".repeat(78);
const sections = [
  "ET: Legacy Web - Third-Party Notices",
  "",
  "This file covers third-party code linked into the browser engine and the",
  "Legacy game modules. ET: Legacy itself and separately downloaded game/mod/map",
  "packages have their own licence terms and are not replaced by this notice.",
];

for (const component of components) {
  sections.push("", separator, `${component.name} (${component.version})`, separator);
  for (const relativePath of component.files) {
    const absolutePath = path.join(repositoryRoot, relativePath);
    const licenceText = (await readFile(absolutePath, "utf8")).trimEnd();
    sections.push("", `Source notice: ${relativePath}`, "", licenceText);
  }
}

await mkdir(path.dirname(outputPath), { recursive: true });
await writeFile(outputPath, `${sections.join("\n")}\n`, "utf8");
console.log(`Wrote ${path.relative(process.cwd(), outputPath)} (${components.length} components)`);
