# sekai-music

从远端获取 Project SEKAI 全部歌曲版本的 TypeScript 库。返回音源 URL、歌名、封面 URL、作者和歌手/歌姬。零运行时依赖，使用标准 `fetch`，支持 Node.js 20+ 与支持 ES modules、fetch、AbortController 的现代浏览器。

运行时从公开服务器读取主数据；不需要本地 JSON，不内置固定曲库，不读写文件。内存缓存默认 15 分钟。首次请求与清除缓存后的请求需要联网。

## 使用

此交付包尚未发布到 npm。把生成的 `sekai-music-0.1.0.tgz` 安装到你的项目：

```bash
pnpm add /path/to/sekai-music-0.1.0.tgz
```

```ts
import { createSekaiMusicClient } from 'sekai-music';

const sekai = createSekaiMusicClient();

// 全部五个区服、全部演唱版本。每个区服保留自己的记录。
const { tracks, errors, complete } = await sekai.listAllTracks();
if (!complete) console.error(errors); // 明确报告失败区服，避免把部分结果当成全库。

const songs = tracks.map(({ id, title, audioUrl, coverUrl, author, singers }) => ({
  id, title, audioUrl, coverUrl, author, singers,
}));

// 只查一个区服；默认 JP。
const jp = await sekai.listTracks({ region: 'jp' });

// 获取一首歌的全部演唱版本，并确认音源、封面实际可用。
const monitoring = await sekai.getMusic(628, {
  region: 'en',
  verifyUrls: true,
});

// 按歌名、作者、作词、编曲、版本名称、歌手搜索，自动使用内存缓存。
const results = await sekai.search('Monitoring', { region: 'en' });

// 用户选中一个版本时验证 URL，缺失时尝试候选地址。
if (results[0]) {
  const song = await sekai.resolveTrack(results[0]);
  if (song.audioStatus === 'available' && song.audioUrl) {
    console.log(song.title, song.audioUrl, song.coverUrl, song.author, song.singers);
  }
}
```

`listTracks` / `listAllTracks` 默认只获取元数据，返回的 URL 为候选地址，状态为 `unchecked`。`resolveTrack` 或 `verifyUrls: true` 才会检查资源。不要将未验证的 URL 当成可下载保证。

需要验证整个曲库时：

```ts
const result = await sekai.listAllTracks({
  verifyUrls: true,
  concurrency: 4,
});
const playable = result.tracks.filter(track => track.audioStatus === 'available');
```

全库验证会产生大量 HTTP 请求；按选中歌曲调用 `resolveTrack` 通常更适合播放器。验证仅请求 HEAD，服务器不支持 HEAD 时尝试 `Range: bytes=0-0` 并取消响应体，不下载完整音乐。状态为 `available` 表示当次响应成功且媒体类型匹配，不代表已经完整解码音频。

## 返回字段

```ts
interface MusicTrack {
  id: string;                  // 例 en:1185，同一首歌的不同演唱版本不会互相覆盖
  region: 'jp' | 'en' | 'cn' | 'tw' | 'kr';
  musicId: number;
  vocalId: number;
  title: string;               // 所选区服原始歌名，例如 Monitoring
  audioUrl: string | null;
  coverUrl: string | null;
  author: string;              // 作曲者；空值时回退到作词者
  composer: string;
  lyricist: string;
  arranger: string;
  singers: string[];           // 关联当前演唱版本的角色/歌手表
  artist: string;              // singers 组合；没有已知歌手时使用 author
  vocalType: string;           // 包含未来出现的新类型
  version: string;             // 版本 caption
  // 其余字段见 src/types.ts 或生成的 TypeScript 声明。
}
```

`author` 不等于演唱者。`singers` 使用所选区服角色表的名称：EN 常为 `Hatsune Miku`，JP 为 `初音ミク`，不进行自行翻译。某角色不在名字表时，`singerDetails` 保留其类型和 ID，`name: null`；不会编造名称。

同一个 musicId 可有多个 vocalId。五区返回结果按区服保留，不跨区强行去重。列表含主数据中所有演唱版本，可能含未发布/已下架/资源缺失的记录；保留 `publishedAt` 和 `vocalPublishedAt` 供业务筛选，时间单位为毫秒。某些版本没有发布时间，返回 null。

## 资源状态与错误

| 状态 | 含义 |
|---|---|
| unchecked | 仅根据远端字段拼接的候选 URL |
| available | 检查时 HTTP 2xx 且 Content-Type 为相应媒体类型或通用二进制 |
| missing | 全部候选均明确返回 404/410；URL 为 null |
| unknown | 403、429、5xx、超时、网络/CORS 错误或不匹配的 Content-Type；URL 为 null |

`audioAttempts` / `coverAttempts` 保留尝试过的 URL、HTTP 状态与错误。单区元数据失败会抛出异常；全区查询在 `errors` 中返回具体区服与异常。取消请求始终抛出，不伪装成部分成功。`DataError` 表示远端结构或表间关联异常，`HttpError` 含 `status` 和 `url`。

## 配置地址、音质、缓存和取消

```ts
const sekai = createSekaiMusicClient({
  masterBaseUrl: 'https://sekai-world.github.io',
  assetBaseUrl: 'https://storage.sekai.best',
  // 主人提供的封面源，为默认值。需填写到桶，不要附加 /music/jacket。
  coverBaseUrl: 'https://storage.exmeaning.com/sekai-cn-assets',
  fallbackToJapaneseAssets: true,
  cacheTtlMs: 15 * 60_000,
  timeoutMs: 15_000,
  retries: 1,
});

const tracks = await sekai.listTracks({ region: 'jp', format: 'flac', length: 'long' });
const previews = await sekai.listTracks({ region: 'jp', format: 'mp3', length: 'short' });

const controller = new AbortController();
const loading = sekai.listAllTracks({ signal: controller.signal });
// controller.abort();
await loading;

sekai.clearCache();
```

还可用 `masterUrls` / `assetUrls` 分别覆盖单一区服的完整目录或桶 URL，以及用 `fetch` 注入代理、鉴权网关或测试 transport。配置示例：

```ts
const sekai = createSekaiMusicClient({
  masterUrls: { jp: 'https://example.com/master/jp' },
  assetUrls: { jp: 'https://example.com/assets/jp' },
  coverBaseUrl: 'https://example.com/covers',
  fetch: globalThis.fetch.bind(globalThis),
});
```

区服为 `jp / en / cn / tw / kr`，繁中仓库和桶自动映射为 `tc`。

音频优先所选区服桶，再尝试 JP 同名资源（可用 `fallbackToJapaneseAssets: false` 禁用）。这是保留所选区服优先的库策略，不复制网站易过期的专属歌曲 ID 清单。跨区回退可能取得 JP 的音频版本；严格要求区服一致时请禁用，实际使用的 URL 与尝试记录始终保留。

封面优先 CN 镜像，再尝试所选区服与允许的 JP 桶；每个源尝试 WebP、PNG。封面使用 `musics.assetbundleName`；音频使用 `musicVocals.assetbundleName`。音乐 19 的原唱封面保留 `_org` 规则。

`long` 是游戏音频，是否原曲完整版查看 `isFullLength`，不是由目录名决定。`fillerSec` 保留前置填充时长；本库不裁剪音频、不改标签，也不转码。

## 浏览器

ESM 构建不依赖 Node.js 内置模块，可由 Vite 等打包器导入，或直接通过 HTTP 提供 `dist/` 后导入 `dist/index.js`。跨域 fetch 受上游 CORS 与网络限制；HTTP 403、CORS 错误与资源不存在分别处理。受限网络可配置自己的服务端代理，不要在浏览器暴露代理凭据。

`examples/browser.html` 是最小浏览器验证页，可在仓库根目录提供静态服务后访问。

## 开发与验证

```bash
pnpm install --frozen-lockfile
pnpm check
pnpm test
pnpm test:live
pnpm pack
```

测试使用最小合成夹具，不打包歌曲数据。实时验证会访问全部五个区服主数据，并验证 JP Monitoring、EN CONNECT:COMMUNE 的音源和封面。公共上游状态可能变化，实时验证独立于离线单元测试。

数据来源：[Sekai World 主数据](https://github.com/Sekai-World)、[Sekai Viewer 音乐页源码](https://github.com/Sekai-World/sekai-viewer/blob/b720f999c67c6b4ec0159611c8b606505de0c7b5/src/pages/music/MusicDetail.tsx)。
