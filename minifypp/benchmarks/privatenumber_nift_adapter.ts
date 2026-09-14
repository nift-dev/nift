// Reference adapter for adding Nift to privatenumber/minification-benchmarks.
// Copy/adapt this file inside that repository's packages/minifiers/minifiers/
// directory and set NIFT_BIN to the Nift executable to benchmark.
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { spawn } from 'node:child_process';

import { createMinifier } from '../utils/create-minifier.ts';

const nift = process.env.NIFT_BIN ?? 'nift';

export default createMinifier(
  'nift',
  {
    default: async ({ code }) => {
      const directory = await mkdtemp(join(tmpdir(), 'nift-minification-benchmark-'));
      const input = join(directory, 'input.js');
      const output = join(directory, 'input.min.js');
      try {
        await writeFile(input, code);
        await new Promise<void>((resolve, reject) => {
          const child = spawn(nift, ['minify', input], { stdio: ['ignore', 'ignore', 'pipe'] });
          let error = '';
          child.stderr.setEncoding('utf8');
          child.stderr.on('data', chunk => { error += chunk; });
          child.on('error', reject);
          child.on('close', code => {
            if (code === 0) resolve();
            else reject(new Error(error || `nift exited with ${code}`));
          });
        });
        return await readFile(output, 'utf8');
      } finally {
        await rm(directory, { recursive: true, force: true });
      }
    },
  },
);
