# Generate a pseudo-Japanese translation (text/ja_pseudo) for engine/buffer tests.
import json, glob, os, re, random
ROOT = os.path.join(os.path.dirname(__file__), '..')
KEEP = re.compile(r'(\{C[0-5]\}|\{x(?!0D)[0-9A-Fa-f]{2}\}|%%|%[-+ #0-9.]*l?[a-zA-Z]|[@~^])')
# many distinct characters to exercise 2-byte codes
pool = [chr(c) for c in range(0x3042, 0x3094)] + [chr(c) for c in range(0x30a2, 0x30f4)]
kanji = '日一国会人年大十二本中長出三同時政事自行社見月分議後前民生連五発間対上部東者党地合市業内相方四定今回新場金員九入選立開手米力学問高代明実円関決子動京全目表戦経通外最言氏現理調体化田当八六約主題下首意法不来作性的要用制治度務強気小七成期公持野協取都和統以機平総加山思家話世受区領多県続進正安設保改数記院女初北午指権心界支第産結百派点教報済書府活原先共得解名交資予川向際査勝面委告軍文反元重近千考判認画海参売利組知案道信策集在件団別物側任引使求所次水半品昨論計死官増係感特情投示変打男基私各始島直両朝革価式確村提運終挙果西勢減台広容必応演電歳住争談能無再位置企真流格有疑口過局少放税検藤町常校料沢裁状工建語球営空職証土与急止送援供可役構木割聞身費付施切由説転食比難防補車優夫研収断井何南石足違消境神番規術護展態導鮮備宅害配副算視条幹独警宮究育席輸訪楽起万着乗店述残想線率病農州武声質念待試族象銀域助労例衛然早張映限親額監環験追審商葉義伝働形景落欧担好退準賞訴辺造英被株頭技低毎医復仕去姿味負閣韓渡失移差衆個門写評課末守若脳極種美岡影命含福蔵量望松非撃佐核観察整段横融型白深字答夜製票況音申様財港識注呼渉達良響阪帰針専推誘停'
pool += list(kanji)
random.seed(7)
def fake(n):
    return ''.join(random.choice(pool) for _ in range(max(1, n)))
def conv(s, nm):
    if not s.strip():
        return s
    if nm in ('ITEMS.LTX', 'MONNAMES.LTX') and s.count('.') == 2:
        return fake(max(2, int(len(s.split('.')[0]) * 0.5))) + '..'
    parts = KEEP.split(s.replace('{x0D}', ' '))
    out = []
    for p in parts:
        if KEEP.fullmatch(p or 'x') and p:
            out.append(p)
        else:
            t = re.sub(r'[<>]', '', p)
            n = int(len(t.strip()) * 0.6)
            if t.strip():
                out.append(fake(n))
    r = ''.join(out)
    if s.startswith('<'):
        r = '「' + r + '」'
    return r
os.makedirs(os.path.join(ROOT, 'text', 'ja_pseudo'), exist_ok=True)
for f in glob.glob(os.path.join(ROOT, 'text', 'en', '*.json')):
    u = json.load(open(f, encoding='utf-8'))
    nm = u['name']
    if nm in ('PRINTER.LTX', 'DEBUG.LTX', 'RCHEATXT.LTX', 'PHEXADEP.LTX', 'CREDITS.LTX', 'SOUND.LTX'):
        continue
    ss = []
    for i, s in enumerate(u['strings']):
        j = conv(s, nm)
        if u['kind'] == 'fixed':
            j = j[:6]
        ss.append(j)
    json.dump({'strings': ss}, open(os.path.join(ROOT, 'text', 'ja_pseudo', nm + '.json'), 'w', encoding='utf-8'), ensure_ascii=False)
print('ok')
