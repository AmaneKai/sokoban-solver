import { readFileSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { Hono } from 'hono'
import { api } from './api.ts'

// import.meta.url (not .dir): Vite rewrites it to the real file when it bundles a config.
const PAGE = join(dirname(fileURLToPath(import.meta.url)), '..', 'dist', 'index.html')

const app = new Hono()
  .all('/api/*', (c) => api.fetch(c.req.raw))
  .get('*', (c) => c.html(readFileSync(PAGE, 'utf8')))

// `bun server/main.ts` serves the single-file build plus the solver API.
export default { port: Number(process.env.PORT ?? 8000), fetch: app.fetch }
