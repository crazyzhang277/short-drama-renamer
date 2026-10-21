#!/usr/bin/env node
'use strict';

/**
 * 短剧视频批量更名工具
 * 命名格式：剧名 - 序号 #short drama #mini drama #micro drama #类型标签
 *
 * 用法：
 *   node rename.js                     交互模式
 *   node rename.js --folder D:\剧集    指定文件夹
 *   node rename.js --undo <记录文件>   撤销上一次改名
 */

const fs = require('fs');
const os = require('os');
const path = require('path');
const readline = require('readline/promises');

const VERSION = '1.0.0';
const FIXED_TAGS = ['short drama', 'mini drama', 'micro drama'];
const FIXED_TAG_STR = FIXED_TAGS.map((t) => '#' + t).join(' ');

const VIDEO_EXTS = new Set([
  '.mp4', '.mkv', '.mov', '.avi', '.wmv', '.flv', '.m4v', '.ts', '.webm',
  '.mpg', '.mpeg', '.rmvb', '.rm', '.3gp', '.m2ts', '.vob', '.ogv', '.divx',
  '.asf', '.f4v',
]);

// 类型标签：英文标签 + 中文说明，最后一个 # 标签从这里选
const TAG_GROUPS = [
  {
    title: '都市豪门',
    tags: [
      ['Billionaire boss', '亿万富翁老板'],
      ['CEO romance', '首席执行官恋情'],
      ['True heiress', '真正的继承人'],
      ['Hidden identity', '隐藏的身份'],
      ['forced marriage', '契约婚姻'],
    ],
  },
  {
    title: '狼人 / 阿尔法',
    tags: [
      ['Alpha', '阿尔法'],
      ['Mate', '马特'],
      ['Werewolf', '狼人'],
    ],
  },
  { title: '离婚复仇', tags: [['Divorce revenge', '离婚复仇']] },
  {
    title: '秘密宝贝 / 单身妈妈',
    tags: [
      ['Secret baby', '秘密宝贝'],
      ['Single mom', '单身妈妈'],
    ],
  },
  {
    title: '继兄 / 禁忌之恋',
    tags: [
      ['Stepbrother romance', '继兄浪漫'],
      ['Forbidden love', '禁忌之爱'],
    ],
  },
  {
    title: '黑手党 / 黑暗浪漫',
    tags: [
      ['Mafia romance', '黑手党浪漫'],
      ['Dark romance', '黑暗浪漫'],
    ],
  },
];
const ALL_TAGS = TAG_GROUPS.flatMap((g) => g.tags);

// ---------- 终端输出 ----------
const useColor = Boolean(process.stdout.isTTY) && !process.env.NO_COLOR;
const C = {
  reset: '\x1b[0m', bold: '\x1b[1m', dim: '\x1b[2m',
  red: '\x1b[31m', green: '\x1b[32m', yellow: '\x1b[33m',
  blue: '\x1b[34m', magenta: '\x1b[35m', cyan: '\x1b[36m', gray: '\x1b[90m',
};
const c = (code, s) => (useColor ? code + s + C.reset : s);

function strWidth(s) {
  let w = 0;
  for (const ch of s) {
    const cp = ch.codePointAt(0);
    if (
      cp >= 0x1100 &&
      (cp <= 0x115f || cp === 0x2329 || cp === 0x232a ||
        (cp >= 0x2e80 && cp <= 0xa4cf && cp !== 0x303f) ||
        (cp >= 0xac00 && cp <= 0xd7a3) ||
        (cp >= 0xf900 && cp <= 0xfaff) ||
        (cp >= 0xfe30 && cp <= 0xfe6f) ||
        (cp >= 0xff00 && cp <= 0xff60) ||
        (cp >= 0xffe0 && cp <= 0xffe6) ||
        (cp >= 0x1f300 && cp <= 0x1f64f) ||
        (cp >= 0x1f900 && cp <= 0x1f9ff) ||
        (cp >= 0x20000 && cp <= 0x3fffd))
    ) {
      w += 2;
    } else {
      w += 1;
    }
  }
  return w;
}

function trunc(s, w) {
  if (strWidth(s) <= w) return s;
  let out = '';
  let cur = 0;
  for (const ch of s) {
    const cw = strWidth(ch);
    if (cur + cw > w - 1) break;
    out += ch;
    cur += cw;
  }
  return out + '…';
}

function padDisplay(s, w) {
  const sw = strWidth(s);
  if (sw >= w) return trunc(s, w);
  return s + ' '.repeat(w - sw);
}

// 中间省略：保留剧名开头和标签结尾，方便核对
function truncMiddle(s, w) {
  if (strWidth(s) <= w) return s;
  const headW = Math.floor((w - 1) * 0.6);
  const tailW = w - 1 - headW;
  let head = '';
  let hw = 0;
  for (const ch of s) {
    const cw = strWidth(ch);
    if (hw + cw > headW) break;
    head += ch;
    hw += cw;
  }
  const chars = [...s];
  let tail = '';
  let tw = 0;
  for (let i = chars.length - 1; i >= 0; i--) {
    const cw = strWidth(chars[i]);
    if (tw + cw > tailW) break;
    tail = chars[i] + tail;
    tw += cw;
  }
  return head + '…' + tail;
}

function banner() {
  console.log('');
  console.log(c(C.cyan, '  ============================================================'));
  console.log(c(C.cyan, '   短剧视频批量更名工具') + c(C.gray, `  v${VERSION}`));
  console.log(c(C.gray, '   剧名 - 序号 #short drama #mini drama #micro drama #类型标签'));
  console.log(c(C.cyan, '  ============================================================'));
}

function die(msg) {
  console.error(c(C.red, '\n[错误] ' + msg));
  process.exit(1);
}

// ---------- 参数解析 ----------
function parseArgs(argv) {
  const opts = {
    folder: null, name: null, start: null, tag: null, pad: null,
    order: null, colon: 'fullwidth', dryRun: false, yes: false,
    undo: null, recursive: false, listTags: false, help: false, version: false,
  };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    const next = () => {
      const v = argv[++i];
      if (v === undefined) die(`参数 ${a} 缺少值`);
      return v;
    };
    switch (a) {
      case '-h': case '--help': opts.help = true; break;
      case '-v': case '--version': opts.version = true; break;
      case '-d': case '--folder': opts.folder = next(); break;
      case '-n': case '--name': opts.name = next(); break;
      case '-s': case '--start': opts.start = parseInt(next(), 10); break;
      case '-t': case '--tag': opts.tag = next(); break;
      case '--pad': opts.pad = parseInt(next(), 10); break;
      case '--order': opts.order = next(); break;
      case '--colon': opts.colon = next(); break;
      case '--undo': opts.undo = next(); break;
      case '--dry-run': opts.dryRun = true; break;
      case '-y': case '--yes': opts.yes = true; break;
      case '-r': case '--recursive': opts.recursive = true; break;
      case '--list-tags': opts.listTags = true; break;
      default:
        if (a.startsWith('-')) die(`未知参数：${a}（用 --help 查看用法）`);
        if (!opts.folder) opts.folder = a;
        else die(`多余的参数：${a}`);
    }
  }
  if (!['fullwidth', 'dash', 'space', 'keep'].includes(opts.colon)) {
    die('--colon 只能是 fullwidth / dash / space / keep');
  }
  return opts;
}

// ---------- 文本处理 ----------
function normalizePunct(s) {
  return String(s || '')
    .replace(/[，、]/g, ',')
    .replace(/：/g, ':')
    .replace(/[‘’‚‛′]/g, "'")
    .replace(/[“”„″]/g, '"')
    .replace(/！/g, '!')
    .replace(/？/g, '?')
    .replace(/（/g, '(')
    .replace(/）/g, ')')
    .replace(/[－—–]/g, '-')
    .replace(/＃/g, '#')
    .replace(/＆/g, '&')
    .replace(/　/g, ' ')
    .replace(/\s+/g, ' ')
    .trim();
}

function toSafeFileName(title, colonMode) {
  let s = title;
  if (colonMode === 'fullwidth') s = s.replace(/:/g, '：');
  else if (colonMode === 'dash') s = s.replace(/:\s*/g, ' - ');
  else if (colonMode === 'space') s = s.replace(/:/g, ' ');
  // keep：保留英文冒号（Windows 上会报错，由校验环节拦下）
  s = s
    .replace(/[\\/]/g, '-')
    .replace(/["<>|?*]/g, '')
    .replace(/\s+/g, ' ')
    .trim()
    .replace(/[. ]+$/g, '');
  return s;
}

function cleanPath(p) {
  let s = String(p || '').trim();
  if ((s.startsWith('"') && s.endsWith('"')) || (s.startsWith("'") && s.endsWith("'"))) {
    s = s.slice(1, -1);
  }
  s = s.trim();
  if (s === '~') s = os.homedir();
  return s;
}

function stamp() {
  const d = new Date();
  const p = (n, w = 2) => String(n).padStart(w, '0');
  return (
    d.getFullYear() + p(d.getMonth() + 1) + p(d.getDate()) + '-' +
    p(d.getHours()) + p(d.getMinutes()) + p(d.getSeconds()) + '-' + p(d.getMilliseconds(), 3)
  );
}

// 保证文件名不重复（同一秒内多次运行也不会互相覆盖）
function uniquePath(dir, base, ext) {
  let p = path.join(dir, base + ext);
  let i = 2;
  while (fs.existsSync(p)) {
    p = path.join(dir, `${base}-${i}${ext}`);
    i++;
  }
  return p;
}

// ---------- 扫描与排序 ----------
function scanVideos(dir, recursive) {
  const out = [];
  const walk = (d) => {
    let entries;
    try {
      entries = fs.readdirSync(d, { withFileTypes: true });
    } catch (e) {
      console.log(c(C.yellow, `  跳过无法读取的文件夹：${d}（${e.message}）`));
      return;
    }
    for (const ent of entries) {
      if (ent.name.startsWith('.')) continue;
      const full = path.join(d, ent.name);
      if (ent.isDirectory()) {
        if (recursive) walk(full);
        continue;
      }
      const ext = path.extname(ent.name);
      if (!VIDEO_EXTS.has(ext.toLowerCase())) continue;
      let st;
      try {
        st = fs.statSync(full);
      } catch {
        continue;
      }
      out.push({
        path: full,
        dir: d,
        file: ent.name,
        base: path.basename(ent.name, ext),
        ext,
        mtime: st.mtimeMs,
        size: st.size,
        ep: null,
      });
    }
  };
  walk(dir);
  return out;
}

function extractEpisode(base) {
  const found = [];
  const push = (n, score, kind) => {
    if (Number.isFinite(n) && n > 0 && n < 10000) found.push({ n, score, kind });
  };
  let m;

  // S01E05 / s1e5
  m = base.match(/(?:^|[^a-z0-9])s\s*0*(\d{1,2})\s*[._\- ]?e\s*0*(\d{1,4})(?![0-9])/i);
  if (m) push(+m[2], 120, 'SxxExx');

  // EP05 / Episode 5 / E05
  m = base.match(/(?:^|[^a-z0-9])(?:episode|ep|e)\s*[._\- ]?\s*0*(\d{1,4})(?![0-9])/i);
  if (m) push(+m[1], 110, 'EP');

  // 第5集 / 第05集
  m = base.match(/第\s*0*(\d{1,4})\s*[集话話期]/);
  if (m) push(+m[1], 130, '第X集');

  // 5集 / 05话
  m = base.match(/(?:^|[^0-9])0*(\d{1,4})\s*[集话話]/);
  if (m) push(+m[1], 125, 'X集');

  // [05] （05） 【05】
  m = base.match(/[\[(（【]\s*0*(\d{1,4})\s*[\])）】]/);
  if (m) push(+m[1], 80, '括号');

  // 结尾数字：剧名 05 / 剧名-05 / 剧名_05
  m = base.match(/(?:^|[ ._\-])0*(\d{1,3})\s*$/);
  if (m) push(+m[1], 60, '结尾数字');

  // 开头数字：05 剧名
  m = base.match(/^\s*0*(\d{1,3})(?=[ ._\-]|$)/);
  if (m) push(+m[1], 50, '开头数字');

  const BAD = new Set([240, 360, 480, 576, 720, 1080, 1440, 2160, 4320, 264, 265]);
  const valid = found.filter((f) => {
    if (f.score >= 100) return true; // 有明确集数标记，直接信任
    if (BAD.has(f.n)) return false; // 分辨率 / 编码
    if (f.n >= 1900 && f.n <= 2099) return false; // 年份
    return true;
  });
  if (!valid.length) return null;
  valid.sort((a, b) => b.score - a.score || a.n - b.n);
  return valid[0];
}

const collator = new Intl.Collator('zh-CN', { numeric: true, sensitivity: 'base' });

function sortFiles(files, mode) {
  const arr = [...files];
  if (mode === 'mtime') {
    return arr.sort((a, b) => a.mtime - b.mtime || collator.compare(a.base, b.base));
  }
  if (mode === 'ep') {
    return arr.sort((a, b) => {
      const ea = a.ep;
      const eb = b.ep;
      if (ea && eb && ea.n !== eb.n) return ea.n - eb.n;
      if (ea && !eb) return -1;
      if (!ea && eb) return 1;
      return collator.compare(a.base, b.base);
    });
  }
  if (mode === 'natural') return arr.sort((a, b) => collator.compare(a.base, b.base));
  // auto
  const detected = arr.filter((f) => f.ep).length;
  return sortFiles(arr, detected >= Math.ceil(arr.length * 0.6) ? 'ep' : 'natural');
}

// ---------- 命名计划 ----------
function buildPlan(files, cfg) {
  return files.map((f, i) => {
    const seq = cfg.start + i;
    const seqStr = cfg.pad > 0 ? String(seq).padStart(cfg.pad, '0') : String(seq);
    const title = `${cfg.drama} - ${seqStr} ${FIXED_TAG_STR} #${cfg.tag}`;
    const target = toSafeFileName(title, cfg.colon) + f.ext;
    return Object.assign({}, f, {
      seq,
      title,
      target,
      targetPath: path.join(f.dir, target),
    });
  });
}

function validatePlan(plan) {
  const issues = [];
  const seen = new Map();
  const sources = new Set(plan.map((p) => p.path.toLowerCase()));

  for (const p of plan) {
    const key = p.targetPath.toLowerCase();
    if (seen.has(key)) {
      issues.push({ fatal: true, msg: `两个视频会改成同一个名字：${p.target}` });
    } else {
      seen.set(key, p);
    }
    if (p.target.length > 200) {
      issues.push({ fatal: true, msg: `新文件名过长（${p.target.length} 字符）：${p.target.slice(0, 40)}…` });
    }
    if (key !== p.path.toLowerCase() && fs.existsSync(p.targetPath) && !sources.has(key)) {
      issues.push({ fatal: true, msg: `目标文件已存在，会被覆盖：${p.target}` });
    }
  }

  if (process.platform === 'win32' && plan.some((p) => p.target.includes(':'))) {
    issues.push({
      fatal: true,
      msg: '文件名里有英文冒号，Windows 不允许。请改用 --colon fullwidth（默认）或 --colon dash。',
    });
  }
  return issues;
}

function printIssues(issues) {
  if (!issues.length) return;
  console.log('');
  for (const it of issues) {
    console.log(c(C.red, '  [!] ' + it.msg));
  }
}

function printPreview(plan, cfg) {
  const cols = process.stdout.columns || 110;
  const idxW = 4;
  const epW = 4;
  const gap = 2;
  const avail = Math.max(40, cols - idxW - epW - gap * 3 - 2);
  let oldW = Math.max(16, Math.min(44, Math.round(avail * 0.34)));
  let newW = avail - oldW;
  if (newW < 26) {
    newW = 26;
    oldW = Math.max(12, avail - newW);
  }

  console.log('');
  console.log(c(C.bold, '命名方案'));
  console.log(c(C.gray, `  剧名：${cfg.drama}`));
  console.log(c(C.gray, `  序号：${cfg.start} 起${cfg.pad > 0 ? `，补零 ${cfg.pad} 位` : '，不补零'}`));
  console.log(c(C.gray, `  标签：${FIXED_TAG_STR} #${cfg.tag}`));
  console.log(c(C.gray, `  共 ${plan.length} 个视频`));
  if (cfg.colon === 'fullwidth' && plan.some((p) => p.title.includes(':'))) {
    console.log(c(C.yellow, '  注意：Windows 文件名不能含英文冒号，文件名里已换成全角“：”；'));
    console.log(c(C.yellow, '        需要英文冒号的完整标题，见改名后生成的 titles 文件。'));
  }
  console.log('');
  console.log(
    c(C.bold, padDisplay('#', idxW) + '  ' + padDisplay('集', epW) + '  ' +
      padDisplay('原文件名', oldW) + '  ' + padDisplay('新文件名', newW))
  );
  console.log(c(C.gray, '─'.repeat(Math.min(cols - 1, idxW + epW + oldW + newW + gap * 3))));

  plan.forEach((p, i) => {
    const ep = p.ep ? String(p.ep.n) : '-';
    const line =
      c(C.gray, padDisplay(String(i + 1), idxW)) + '  ' +
      padDisplay(ep, epW) + '  ' +
      padDisplay(p.file, oldW) + '  ' +
      c(C.green, padDisplay(truncMiddle(p.target, newW), newW));
    console.log(line);
  });
}

// ---------- 执行改名 ----------
async function executePlan(plan, folder, cfg) {
  const todo = plan.filter((p) => p.targetPath.toLowerCase() !== p.path.toLowerCase());

  const outDir = path.join(folder, '.rename-tool');
  fs.mkdirSync(outDir, { recursive: true });
  const ts = stamp();
  const self = process.argv[1] || 'rename.js';

  // 先写撤销记录：即使改名中途崩溃，也能照记录恢复
  let logPath = null;
  if (todo.length) {
    logPath = uniquePath(outDir, `undo-${ts}`, '.json');
    fs.writeFileSync(
      logPath,
      JSON.stringify(
        { createdAt: new Date().toISOString(), folder, entries: plan.map((p) => ({ from: p.path, to: p.targetPath })) },
        null,
        2
      ),
      'utf8'
    );
  }

  if (todo.length) {
    const items = todo.map((p) => ({ src: p.path, tmp: null, final: p.targetPath, file: p.file, target: p.target }));
    let done = 0;
    let cleanRollback = true;
    try {
      for (const it of items) {
        it.tmp = path.join(
          path.dirname(it.src),
          `.rn-tmp-${process.pid}-${Math.random().toString(36).slice(2, 8)}${path.extname(it.src)}`
        );
        await fs.promises.rename(it.src, it.tmp);
      }
      for (const it of items) {
        await fs.promises.rename(it.tmp, it.final);
        it.tmp = null;
        done++;
        console.log(c(C.gray, `  [OK] ${it.file}  ->  ${it.target}`));
      }
    } catch (err) {
      console.error(c(C.red, `\n改名失败：${err.message}`));
      console.error(c(C.yellow, '正在回滚……'));
      for (const it of [...items].reverse()) {
        try {
          if (it.tmp && fs.existsSync(it.tmp)) await fs.promises.rename(it.tmp, it.src);
          else if (fs.existsSync(it.final) && !fs.existsSync(it.src)) await fs.promises.rename(it.final, it.src);
        } catch (e2) {
          cleanRollback = false;
          console.error(c(C.red, `  回滚失败：${it.src}（${e2.message}）`));
        }
      }
      if (cleanRollback && logPath) {
        try { fs.unlinkSync(logPath); } catch { /* ignore */ }
      }
      throw new Error('改名未完成，已尽力回滚，请检查文件夹。');
    }
  }

  const titlesPath = uniquePath(outDir, `titles-${ts}`, '.txt');
  fs.writeFileSync(titlesPath, plan.map((p) => p.title).join('\r\n') + '\r\n', 'utf8');

  console.log('');
  if (todo.length) {
    console.log(c(C.green, `[完成] ${todo.length} 个文件已改名。`));
    console.log(c(C.gray, `  撤销记录：${logPath}`));
    console.log(c(C.gray, `  标题清单：${titlesPath}`));
    console.log(c(C.gray, `  撤销命令：node "${self}" --undo "${logPath}"`));
  } else {
    console.log(c(C.green, '所有文件已经是目标名称，没有需要改的。'));
    console.log(c(C.gray, `  标题清单：${titlesPath}`));
  }
}

// ---------- 撤销 ----------
async function undo(logFile) {
  const file = path.resolve(cleanPath(logFile));
  if (!fs.existsSync(file)) die(`找不到撤销记录：${file}`);
  let log;
  try {
    log = JSON.parse(fs.readFileSync(file, 'utf8'));
  } catch (e) {
    die(`撤销记录格式不对：${e.message}`);
  }
  if (!Array.isArray(log.entries) || !log.entries.length) die('撤销记录里没有可恢复的文件。');

  let ok = 0;
  let skip = 0;
  for (const e of [...log.entries].reverse()) {
    if (!e.to || !e.from) continue;
    if (!fs.existsSync(e.to)) {
      console.log(c(C.gray, `  跳过（找不到）：${e.to}`));
      skip++;
      continue;
    }
    if (fs.existsSync(e.from)) {
      console.log(c(C.gray, `  跳过（原名已存在）：${e.from}`));
      skip++;
      continue;
    }
    try {
      await fs.promises.rename(e.to, e.from);
      ok++;
      console.log(c(C.gray, `  恢复：${path.basename(e.to)}  ->  ${path.basename(e.from)}`));
    } catch (err) {
      console.log(c(C.red, `  失败：${e.to}（${err.message}）`));
      skip++;
    }
  }
  console.log('');
  console.log(c(C.green, `[完成] 恢复 ${ok} 个文件，跳过/失败 ${skip} 个。`));
}

// ---------- 交互辅助 ----------
function printTagTable() {
  console.log('');
  console.log(c(C.bold, '选择一个类型标签（放在最后面的 #）：'));
  console.log('');
  let i = 0;
  for (const g of TAG_GROUPS) {
    console.log(c(C.cyan, `  【${g.title}】`));
    for (const [en, zh] of g.tags) {
      i++;
      console.log(`  ${String(i).padStart(2)}. ${en.padEnd(22)} ${c(C.gray, zh)}`);
    }
  }
  console.log('');
  console.log(c(C.gray, '  0. 自定义标签（直接输入文字，例如 Revenge romance）'));
}

async function chooseTag(ask) {
  printTagTable();
  for (;;) {
    const ans = String(await ask('编号或标签文字', '')).trim();
    if (!ans) continue;
    if (ans === '0') {
      const custom = String(await ask('自定义标签（不要带 #）', '')).trim().replace(/^#+/, '');
      if (custom) return custom;
      continue;
    }
    if (/^\d+$/.test(ans)) {
      const idx = parseInt(ans, 10);
      if (idx >= 1 && idx <= ALL_TAGS.length) return ALL_TAGS[idx - 1][0];
      console.log(c(C.red, '  编号超出范围，请重新输入。'));
      continue;
    }
    return ans.replace(/^#+/, '');
  }
}

async function chooseOrder(ask, detected, total) {
  const auto = detected >= Math.ceil(total * 0.6) ? 'ep' : 'natural';
  console.log('');
  console.log(c(C.bold, '排序方式'));
  console.log(`  1) 自动识别集数（已识别 ${detected}/${total}）${auto === 'ep' ? c(C.green, '  ← 推荐') : ''}`);
  console.log(`  2) 文件名自然排序（1, 2, 10）${auto === 'natural' ? c(C.green, '  ← 推荐') : ''}`);
  console.log('  3) 修改时间（从早到晚）');
  const ans = String(await ask('选择', auto === 'ep' ? '1' : '2')).trim();
  if (ans === '3') return 'mtime';
  if (ans === '2') return 'natural';
  return 'ep';
}

function guessDrama(folder) {
  const base = path.basename(folder);
  if (!base || /^[A-Za-z]:\\?$/.test(base)) return '';
  return normalizePunct(base).slice(0, 80);
}

async function excludeFiles(ask, files) {
  const ans = String(await ask('要排除的序号（逗号分隔，如 3,5；直接回车返回）', '')).trim();
  if (!ans) return files;
  const set = new Set(
    ans.split(/[,，\s]+/).map((x) => parseInt(x, 10)).filter((n) => Number.isFinite(n) && n > 0)
  );
  const kept = files.filter((f, i) => !set.has(i + 1));
  console.log(c(C.gray, `  已排除 ${files.length - kept.length} 个，剩余 ${kept.length} 个（序号会重新连续排列）。`));
  return kept;
}

// ---------- 交互流程 ----------
function makeAsker() {
  if (process.stdin.isTTY) {
    const rl = readline.createInterface({
      input: process.stdin,
      output: process.stdout,
      terminal: true,
    });
    return {
      ask: async (q, def = '') => {
        const hint = def !== '' ? c(C.gray, ` [${def}]`) : '';
        let ans;
        try {
          ans = await rl.question(`${q}${hint}: `);
        } catch {
          ans = '';
        }
        ans = String(ans == null ? '' : ans).trim();
        return ans === '' ? def : ans;
      },
      close: () => rl.close(),
    };
  }

  // 非终端（管道 / 重定向）：一次性读完输入，按行依次回答
  let raw = '';
  try {
    raw = fs.readFileSync(0, 'utf8');
  } catch {
    raw = '';
  }
  if (!raw.trim()) {
    die('当前不是交互终端。请直接双击 start.bat，或使用 --yes --name --tag 批量模式。');
  }
  const lines = raw.split(/\r?\n/);
  let idx = 0;
  return {
    ask: async (q, def = '') => {
      const hint = def !== '' ? c(C.gray, ` [${def}]`) : '';
      const ans = idx < lines.length ? String(lines[idx++]).trim() : '';
      process.stdout.write(`${q}${hint}: ${ans}\n`);
      return ans === '' ? def : ans;
    },
    close: () => {},
  };
}

async function runInteractive(opts) {
  const { ask, close } = makeAsker();

  try {
    banner();

    let folder = path.resolve(cleanPath(opts.folder || (await ask('视频所在文件夹（可直接把文件夹拖进来）', process.cwd()))));
    if (!fs.existsSync(folder) || !fs.statSync(folder).isDirectory()) die(`找不到文件夹：${folder}`);

    let files = scanVideos(folder, opts.recursive);
    if (!files.length) {
      die(`这个文件夹里没有视频文件。\n       支持的格式：${[...VIDEO_EXTS].join(' ')}`);
    }
    files.forEach((f) => { f.ep = extractEpisode(f.base); });
    const detected = files.filter((f) => f.ep).length;
    console.log('');
    console.log(c(C.gray, `  扫描到 ${files.length} 个视频，其中 ${detected} 个能识别出集数。`));

    const order = opts.order || (await chooseOrder(ask, detected, files.length));
    files = sortFiles(files, order);

    let drama = opts.name ? normalizePunct(opts.name) : '';
    if (!drama) {
      const guess = guessDrama(folder);
      while (!drama) {
        drama = normalizePunct(await ask('剧名（英文；逗号/冒号/单引号会自动转成英文标点）', guess));
        if (!drama) console.log(c(C.red, '  剧名不能为空。'));
      }
    } else if (opts.name && drama !== opts.name.trim()) {
      console.log(c(C.yellow, `  已规范标点：${drama}`));
    }

    let start = opts.start;
    if (start == null) {
      start = parseInt(await ask('起始序号', '1'), 10);
      if (!Number.isFinite(start) || start < 0) start = 1;
    }

    let pad = opts.pad;
    if (pad == null) {
      pad = parseInt(await ask('补零位数（0 = 1,2,3；2 = 01,02,03）', '0'), 10);
      if (!Number.isFinite(pad) || pad < 0) pad = 0;
    }

    let tag = opts.tag ? resolveTag(opts.tag) : '';
    if (!tag) tag = await chooseTag(ask);

    const cfg = { drama, start, pad, tag, colon: opts.colon };
    let active = files;

    for (;;) {
      const plan = buildPlan(active, cfg);
      printPreview(plan, cfg);
      const issues = validatePlan(plan);
      printIssues(issues);

      const action = String(await ask('确认执行？[y] 执行  [n] 取消  [e] 排除文件  [r] 重新设置', 'y')).toLowerCase();

      if (action === 'n') {
        console.log(c(C.gray, '\n已取消，没有改动任何文件。'));
        return;
      }
      if (action === 'e') {
        active = await excludeFiles(ask, active);
        continue;
      }
      if (action === 'r') {
        const g = guessDrama(folder);
        drama = normalizePunct(await ask('剧名', drama || g));
        if (!drama) drama = cfg.drama;
        const s = parseInt(await ask('起始序号', String(cfg.start)), 10);
        if (Number.isFinite(s) && s >= 0) start = s;
        const p2 = parseInt(await ask('补零位数', String(cfg.pad)), 10);
        if (Number.isFinite(p2) && p2 >= 0) pad = p2;
        tag = await chooseTag(ask);
        cfg.drama = drama;
        cfg.start = start;
        cfg.pad = pad;
        cfg.tag = tag;
        continue;
      }
      if (issues.some((it) => it.fatal)) {
        console.log(c(C.red, '\n存在必须解决的问题，不能执行。请排除文件或重新设置。'));
        continue;
      }
      await executePlan(plan, folder, cfg);
      return;
    }
  } finally {
    close();
  }
}

function resolveTag(input) {
  const raw = String(input || '').trim().replace(/^#+/, '');
  if (/^\d+$/.test(raw)) {
    const idx = parseInt(raw, 10);
    if (idx >= 1 && idx <= ALL_TAGS.length) return ALL_TAGS[idx - 1][0];
  }
  const hit = ALL_TAGS.find(([en]) => en.toLowerCase() === raw.toLowerCase());
  return hit ? hit[0] : raw;
}

// ---------- 非交互批量 ----------
async function runBatch(opts) {
  if (!opts.name) die('缺少 --name（剧名）');
  if (!opts.tag) die('缺少 --tag（类型标签），可用 --list-tags 查看全部');
  const folder = path.resolve(cleanPath(opts.folder || '.'));
  if (!fs.existsSync(folder) || !fs.statSync(folder).isDirectory()) die(`找不到文件夹：${folder}`);

  let files = scanVideos(folder, opts.recursive);
  if (!files.length) die('没有找到视频文件。');
  files.forEach((f) => { f.ep = extractEpisode(f.base); });
  files = sortFiles(files, opts.order || 'auto');

  const cfg = {
    drama: normalizePunct(opts.name),
    start: opts.start == null ? 1 : opts.start,
    pad: opts.pad == null ? 0 : opts.pad,
    tag: resolveTag(opts.tag),
    colon: opts.colon,
  };
  if (!cfg.drama) die('剧名不能为空');

  const plan = buildPlan(files, cfg);
  printPreview(plan, cfg);
  const issues = validatePlan(plan);
  printIssues(issues);
  if (issues.some((it) => it.fatal)) die('存在冲突，已停止（没有改动任何文件）。');
  if (opts.dryRun) {
    console.log(c(C.yellow, '\n预览模式：没有改动任何文件。'));
    return;
  }
  await executePlan(plan, folder, cfg);
}

// ---------- 帮助 ----------
function printHelp() {
  console.log(`
短剧视频批量更名工具 v${VERSION}

用法：
  node rename.js [选项]

交互模式（推荐）：
  node rename.js
  node rename.js --folder "D:\\短剧\\Dignity Reborn"

批量模式（不给 --yes 时仍是交互模式）：
  node rename.js --folder "D:\\短剧\\某剧" --name "Dignity Reborn" --tag "Billionaire boss" --yes
  node rename.js --folder "D:\\短剧\\某剧" --name "Dignity Reborn" --tag "Billionaire boss" --yes --dry-run

选项：
  -d, --folder <路径>   视频文件夹（默认当前目录）
  -n, --name <剧名>     剧名，自动把中文标点转成英文标点
  -s, --start <数字>    起始序号，默认 1
      --pad <位数>      序号补零位数，默认 0（1,2,3）；2 表示 01,02,03
  -t, --tag <标签>      类型标签（编号或英文标签，见 --list-tags）
      --order <方式>    ep | natural | mtime | auto（默认 auto）
      --colon <方式>    fullwidth（默认，全角“：”）| dash | space | keep
  -r, --recursive       包含子文件夹
      --dry-run         只预览，不改名
  -y, --yes             跳过交互，直接执行（需同时给 --name 和 --tag）
      --undo <文件>     撤销某次改名（传入生成的 undo-*.json）
      --list-tags       列出所有标签
  -h, --help            显示帮助
  -v, --version         显示版本

命名格式：
  剧名 - 序号 #short drama #mini drama #micro drama #类型标签

排序：
  自动识别集数（S01E05 / EP05 / 第5集 / [05] / 结尾数字）
  → 识别不到时用文件名自然排序（1, 2, 10）
`);
}

// ---------- 入口 ----------
async function main() {
  const opts = parseArgs(process.argv.slice(2));
  if (opts.help) return printHelp();
  if (opts.version) return console.log(VERSION);
  if (opts.listTags) return printTagTable();
  if (opts.undo) return undo(opts.undo);
  if (opts.yes) return runBatch(opts);
  return runInteractive(opts);
}

process.on('SIGINT', () => {
  console.log('\n已取消。');
  process.exit(130);
});

main().catch((err) => {
  console.error(c(C.red, `\n[错误] ${err && err.message ? err.message : err}`));
  process.exit(1);
});
