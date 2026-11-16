// Cross-platform command line runner. Run it through bun: `bun run <command>`.
import { existsSync, mkdtempSync, readdirSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { basename, join, resolve } from "node:path";
import {
  buildSearchTree,
  decodeExploration,
  writeTelemetryFiles,
} from "@sokobot/core";
import { renderSummary } from "./summary.ts";

const ROOT = resolve(import.meta.dir, "..");
const WEB = join(ROOT, "web");
const SOLVERS = join(ROOT, "solvers");
const PORTFOLIO = join(SOLVERS, "portfolio");
const MAPS = join(ROOT, "maps");
const DATA = join(ROOT, "data");
const WIN = process.platform === "win32";
const exe = (name: string) => join(PORTFOLIO, "bin", WIN ? `${name}.exe` : name);
const MAKE = WIN ? "mingw32-make" : "make";

const paint = (code: number, text: string) => `\x1b[${code}m${text}\x1b[0m`;
const step = (text: string) =>
  console.log(`\n${paint(36, "›")} ${paint(1, text)}`);
const ok = (text: string) => console.log(`${paint(32, "✔")}  ${text}`);
const warn = (text: string) => console.log(`${paint(33, "⚠")}  ${text}`);

function die(text: string): never {
  console.error(`\n${paint(31, "✘  error:")} ${text}`);
  process.exit(1);
}

async function run(
  command: string[],
  cwd: string,
  env: Record<string, string> = {},
) {
  let proc: ReturnType<typeof Bun.spawn>;
  try {
    proc = Bun.spawn(command, {
      cwd,
      env: { ...process.env, ...env },
      stdio: ["inherit", "inherit", "inherit"],
    });
  } catch {
    die(`could not run \`${command[0]}\`. Check that it is installed and on your PATH.`);
  }

  return proc.exited;
}

async function must(
  command: string[],
  cwd: string,
  env?: Record<string, string>,
) {
  const code = await run(command, cwd, env);
  if (code !== 0) die(`\`${command.join(" ")}\` exited with ${code}`);
}

const bun = (...args: string[]) => [process.execPath, ...args];

async function installDependencies() {
  if (existsSync(join(ROOT, "node_modules"))) return;

  step("Installing dependencies");
  await must(bun("install"), ROOT);
}

/** Every folder in solvers/ that has a Makefile. */
const solverDirs = () =>
  readdirSync(SOLVERS)
    .filter((name) => existsSync(join(SOLVERS, name, "Makefile")))
    .map((name) => join(SOLVERS, name))
    .filter((dir) => {
      // A solver can name the one system it builds on, e.g. asm needs linux.
      const file = join(dir, "solver.json");
      const platform = existsSync(file) ? JSON.parse(readFileSync(file, "utf8")).platform : undefined;
      return !platform || [platform].flat().includes(process.platform);
    });

function ensureNativeBuilt() {
  if (!existsSync(exe("sokosolve")))
    warn(
      "native solver not built. Use the Build button in the UI or `bun run build`.",
    );
}

/** Solves one map. By default it traces the run and exports data/<date>_<time>_<map>/. --showcase skips both. */
async function solve(args: string[]) {
  const flags = new Map<string, string>();
  const settings: string[] = [];
  const positional: string[] = [];
  for (let i = 0; i < args.length; i++) {
    const arg = args[i]!;
    if (arg === "--showcase" || arg === "--summary") flags.set(arg, "");
    else if (arg === "--set") settings.push(args[++i] ?? "");
    else if (arg.startsWith("--")) flags.set(arg, args[++i] ?? "");
    else positional.push(arg);
  }
  const [target] = positional;
  if (!target)
    die("usage: bun run solve <map> [--showcase] [--summary] [--thread N] [--pops N] [--seconds S] [--set key=value]...");

  const file = existsSync(target)
    ? resolve(target)
    : join(MAPS, target.endsWith(".txt") ? target : `${target}.txt`);
  if (!existsSync(file)) die(`map not found: ${target}`);
  const mapName = basename(file, ".txt");
  const stdin = readFileSync(file);

  if (flags.has("--showcase")) {
    step(`Showcase: ${mapName}`);
    await must([MAKE, "solve"], PORTFOLIO);
    const proc = Bun.spawn([exe("sokosolve")], { stdin, stdout: "pipe" });
    const [ms = "", moves = ""] = (await new Response(proc.stdout).text()).split("\n");
    await proc.exited;
    console.log(`${moves ? moves : "no solution"}\n${paint(2, `${ms} ms`)}`);
    return;
  }

  step(`Telemetry: ${mapName}`);
  await must([MAKE, "trace"], PORTFOLIO);
  const traceArgs: string[] = [];
  for (const flag of ["--thread", "--pops", "--seconds"])
    if (flags.has(flag)) traceArgs.push(flag, flags.get(flag)!);
  for (const setting of settings) traceArgs.push("--set", setting);

  const proc = Bun.spawn([exe("sokotrace"), ...traceArgs], {
    stdin,
    stdout: "pipe",
    stderr: "inherit",
  });
  const output = Buffer.from(await new Response(proc.stdout).arrayBuffer());
  if ((await proc.exited) !== 0) die("sokotrace failed");

  const buffer = output.buffer.slice(
    output.byteOffset,
    output.byteOffset + output.byteLength,
  ) as ArrayBuffer;
  const tree = buildSearchTree(decodeExploration(buffer));

  if (flags.has("--summary")) console.log(renderSummary(mapName, tree, process.stdout.columns));

  for (const file of writeTelemetryFiles(DATA, mapName, tree)) ok(join("data", file));
}

const commands: Record<
  string,
  { help: string; run: (args: string[]) => Promise<void> }
> = {
  build: {
    help: "compile every solver in solvers/",
    async run() {
      step("Building solvers (C++20)");
      const started = Date.now();
      for (const dir of solverDirs()) {
        console.log(paint(2, `  ${basename(dir)}`));
        const targets = dir === PORTFOLIO ? ["solve", "trace", "bench"] : ["solve"];
        await must([MAKE, ...targets], dir);
      }
      ok(`solvers built (${Math.round((Date.now() - started) / 1000)}s)`);
    },
  },

  solve: {
    help: "solve a map and export telemetry to data/ (--showcase: no telemetry, --summary: print a summary)",
    run: solve,
  },

  serve: {
    help: "start the web UI with hot reload [port]",
    async run([port]) {
      await installDependencies();
      ensureNativeBuilt();
      await must(bun("run", "dev", ...(port ? ["--port", port] : [])), WEB);
    },
  },

  start: {
    help: "compile the UI into one HTML file and serve it [port]",
    async run([port = "8000"]) {
      await installDependencies();
      ensureNativeBuilt();
      step("Building the web UI");
      await must(bun("run", "build"), WEB);
      await must(bun("run", "start"), WEB, { PORT: port });
    },
  },

  dev: {
    help: "build the solvers, then serve [port]",
    async run(args) {
      await commands.build!.run([]);
      await commands.serve!.run(args);
    },
  },

  bench: {
    help: "benchmark every map [timeout seconds = 15] [out.csv]",
    async run([timeout = "15", csv]) {
      const names = readdirSync(MAPS)
        .filter((file) => file.endsWith(".txt"))
        .map((file) => file.slice(0, -4))
        .sort(
          new Intl.Collator("en", { numeric: true, sensitivity: "base" })
            .compare,
        );
      if (names.length === 0) die("no .txt files found in maps/");

      step("Building sokobench");
      await must([MAKE, "bench"], PORTFOLIO);

      const list = join(
        mkdtempSync(join(tmpdir(), "sokobot-bench-")),
        "masterlist.txt",
      );
      writeFileSync(list, names.join("\n") + "\n");
      console.log(
        paint(2, `\n  timeout: ${timeout}s  |  maps: ${names.length}\n`),
      );
      await must(
        [exe("sokobench"), list, MAPS, timeout, ...(csv ? [resolve(csv)] : [])],
        ROOT,
      );
    },
  },

  clean: {
    help: "remove the solver builds (objects and binaries)",
    async run() {
      step("Cleaning build artifacts");
      for (const dir of solverDirs()) await must([MAKE, "clean"], dir);
      ok("cleaned");
    },
  },
};

const [name = "help", ...args] = process.argv.slice(2);
const command = commands[name];

if (!command) {
  console.log(`\n${paint(1, "  sokobot")}`);
  console.log(`${paint(2, "  sokoban solver: C++ solvers / web UI")}\n`);
  for (const [key, { help }] of Object.entries(commands))
    console.log(`    bun run ${paint(36, key.padEnd(6))} ${help}`);
  console.log();
  process.exit(name === "help" ? 0 : 1);
}

await command.run(args);
