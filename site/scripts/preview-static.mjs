import { createReadStream, existsSync, statSync } from 'node:fs'
import { createServer } from 'node:http'
import { extname, join, normalize, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const __dirname = fileURLToPath(new URL('.', import.meta.url))
const publicDir = resolve(__dirname, '..', '.output', 'public')

if (!existsSync(publicDir)) {
  console.error('未找到静态构建产物 site/.output/public，请先在 site/ 目录运行 pnpm generate。')
  process.exit(1)
}

const port = Number(process.env.PORT || 4174)
const host = process.env.HOST || '127.0.0.1'

const MIME_TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.webp': 'image/webp',
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.ico': 'image/x-icon',
  '.ttf': 'font/ttf',
  '.woff': 'font/woff',
  '.woff2': 'font/woff2',
  '.txt': 'text/plain; charset=utf-8',
  '.md': 'text/markdown; charset=utf-8',
  '.pem': 'application/x-pem-file',
  '.glb': 'model/gltf-binary',
  '.wasm': 'application/wasm'
}

function resolveFile(urlPath) {
  const clean = normalize(decodeURIComponent(urlPath.split('?')[0] || '/')).replace(
    /^(\.\.[/\\])+/,
    ''
  )
  const candidate = join(publicDir, clean)
  if (!candidate.startsWith(publicDir)) return null

  if (existsSync(candidate) && statSync(candidate).isFile()) {
    return candidate
  }
  const indexHtml = join(candidate, 'index.html')
  if (existsSync(indexHtml) && statSync(indexHtml).isFile()) {
    return indexHtml
  }
  const withHtml = `${candidate}.html`
  if (existsSync(withHtml) && statSync(withHtml).isFile()) {
    return withHtml
  }
  return null
}

const server = createServer((req, res) => {
  const rawUrl = req.url || '/'
  if (rawUrl === '/docs' || rawUrl === '/docs/') {
    res.writeHead(302, { Location: '/docs/getting-started' })
    res.end()
    return
  }

  const filePath = resolveFile(rawUrl)
  if (!filePath) {
    const notFound = join(publicDir, '404.html')
    res.writeHead(404, { 'Content-Type': 'text/html; charset=utf-8' })
    if (existsSync(notFound)) {
      createReadStream(notFound).pipe(res)
    } else {
      res.end('Not Found')
    }
    return
  }

  const mime = MIME_TYPES[extname(filePath).toLowerCase()] || 'application/octet-stream'
  res.writeHead(200, { 'Content-Type': mime })
  createReadStream(filePath).pipe(res)
})

server.listen(port, host, () => {
  console.log(`RodakOS static preview ready at http://${host}:${port}`)
})
