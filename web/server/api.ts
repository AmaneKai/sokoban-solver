import { Hono, type Context } from 'hono'
import { LocalEngineService } from '@sokobot/core'

/** One engine per server process. It runs the native binaries as child processes. */
export const engine = new LocalEngineService()

type Body = Record<string, unknown>

const readBody = async (c: Context): Promise<Body> => await c.req.json().catch(() => ({}))

export const api = new Hono()
  .basePath('/api')
  .use(async (c, next) => {
    await next()
    c.header('Cache-Control', 'no-store')
  })
  .onError((error, c) => c.json({ error: String(error) }, 500))
  .notFound((c) => c.json({ error: 'not found' }, 404))
  .get('/maps', async (c) => c.json(await engine.listMaps()))
  .get('/maps/:name', async (c) => {
    try {
      return c.json(await engine.loadMap(c.req.param('name')))
    } catch {
      return c.json({ error: 'no such map' }, 404)
    }
  })
  .get('/solvers', async (c) => c.json(await engine.listSolvers()))
  .post('/build', async (c) => {
    const body = await readBody(c)
    const result = await engine.build(typeof body.solver === 'string' ? body.solver : undefined)
    return result.ok ? c.json(result) : c.json(result, 500)
  })
  .post('/solve', async (c) => {
    const body = await readBody(c)
    const error = await engine.startSolve(
      String(body.map ?? ''),
      Number(body.timeout) || 0,
      Number.isFinite(Number(body.thread)) ? Number(body.thread) : -1,
      typeof body.solver === 'string' ? body.solver : undefined,
    )
    return error ? c.json({ error }, 409) : c.json(await engine.solveStatus())
  })
  .get('/solve/status', async (c) => c.json(await engine.solveStatus()))
  .post('/solve/cancel', async (c) => c.json(await engine.cancelSolve()))
  .post('/explore', async (c) => {
    const body = await readBody(c)
    const error = await engine.startExplore(
      String(body.map ?? ''),
      Number(body.thread) || 0,
      Number(body.pops) || 0,
      typeof body.solver === 'string' ? body.solver : undefined,
    )
    return error ? c.json({ error }, 409) : c.json(await engine.exploreStatus())
  })
  .get('/explore/status', async (c) => c.json(await engine.exploreStatus()))
  .get('/explore/result', (c) => {
    const bytes = engine.exploreResultBytes()
    if (!bytes) return c.json({ error: 'no explore result available' }, 404)

    return c.body(bytes, 200, { 'Content-Type': 'application/octet-stream' })
  })
  .post('/explore/stop', async (c) => c.json(await engine.stopExplore()))
