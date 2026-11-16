import { getRequestListener } from '@hono/node-server'
import { svelte } from '@sveltejs/vite-plugin-svelte'
import tailwindcss from '@tailwindcss/vite'
import { defineConfig, type Plugin } from 'vite'
import { viteSingleFile } from 'vite-plugin-singlefile'
import { api } from './server/api.ts'

const listener = getRequestListener(api.fetch)

const sokobotApi = (): Plugin => ({
  name: 'sokobot-api',
  configureServer: (server) =>
    void server.middlewares.use((req, res, next) =>
      req.url?.startsWith('/api/') ? void listener(req, res) : next(),
    ),
})

// the whole UI ships as one minified dist/index.html (js + css inlined)
export default defineConfig({
  plugins: [tailwindcss(), svelte(), viteSingleFile(), sokobotApi()],
  server: { port: 8000 },
})
