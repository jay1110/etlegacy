#!/usr/bin/env node

import { createHash } from 'node:crypto';
import { createReadStream } from 'node:fs';
import { readdir, stat, writeFile } from 'node:fs/promises';
import { resolve, relative, dirname, extname, basename } from 'node:path';
import { mkdir } from 'node:fs/promises';

const inputRoot = resolve(process.argv[2] || 'dist/etlegacy-web');
const outputFile = resolve(process.argv[3] || 'compliance/distribution-inventory.json');

function slash(path) {
  return path.replaceAll('\\', '/');
}

function classify(path) {
  const name = basename(path).toLowerCase();
  const extension = extname(name);
  if (/^pak[012]\.pk3$/.test(name)) return 'original-game-data';
  if (extension === '.pk3') return path.startsWith('etmain/') ? 'map-or-game-pk3' : 'mod-pk3';
  if (name.endsWith('.wasm32.so')) return 'wasm-side-module';
  if (extension === '.wasm') return 'wasm-engine';
  if (extension === '.js' || extension === '.mjs') return 'javascript';
  if (extension === '.html') return 'web-page';
  if (/^(copying|licen[cs]e|notice|copyright|eula)/i.test(name)) return 'licence-or-notice';
  if (extension === '.zip') return 'archive';
  if (/\.(png|jpe?g|gif|svg|tga|webp|ico)$/.test(name)) return 'image';
  if (/\.(ogg|wav|mp3|opus)$/.test(name)) return 'audio';
  if (/\.(ttf|otf|woff2?)$/.test(name)) return 'font';
  if (/\.(json|cfg|config|txt|md|rtf|yml|yaml)$/.test(name)) return 'data-or-documentation';
  return 'other';
}

async function digest(path) {
  const hash = createHash('sha256');
  await new Promise((accept, reject) => {
    const stream = createReadStream(path);
    stream.on('data', chunk => hash.update(chunk));
    stream.on('error', reject);
    stream.on('end', accept);
  });
  return hash.digest('hex');
}

async function walk(directory, files = []) {
  const entries = await readdir(directory, { withFileTypes: true });
  entries.sort((a, b) => a.name.localeCompare(b.name));
  for (const entry of entries) {
    const path = resolve(directory, entry.name);
    if (entry.isDirectory()) await walk(path, files);
    else if (entry.isFile()) files.push(path);
  }
  return files;
}

let rootStat;
try {
  rootStat = await stat(inputRoot);
} catch {
  console.error(`Distribution directory does not exist: ${inputRoot}`);
  process.exit(2);
}
if (!rootStat.isDirectory()) {
  console.error(`Distribution path is not a directory: ${inputRoot}`);
  process.exit(2);
}

const paths = await walk(inputRoot);
const files = [];
for (const absolutePath of paths) {
  const info = await stat(absolutePath);
  const path = slash(relative(inputRoot, absolutePath));
  files.push({
    path,
    type: classify(path),
    size: info.size,
    sha256: await digest(absolutePath),
  });
}

const countsByType = {};
let totalBytes = 0;
for (const file of files) {
  countsByType[file.type] = (countsByType[file.type] || 0) + 1;
  totalBytes += file.size;
}

const inventory = {
  schemaVersion: 1,
  generatedAt: new Date().toISOString(),
  distributionRoot: slash(relative(process.cwd(), inputRoot)) || '.',
  fileCount: files.length,
  totalBytes,
  countsByType,
  files,
};

await mkdir(dirname(outputFile), { recursive: true });
await writeFile(outputFile, `${JSON.stringify(inventory, null, 2)}\n`, 'utf8');
console.log(`Inventoried ${files.length} files (${totalBytes} bytes) from ${inputRoot}`);
console.log(`Wrote ${outputFile}`);
