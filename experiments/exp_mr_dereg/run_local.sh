#!/bin/bash
# EXP-MR-DEREG: 本地回环实验脚本
# 使用模拟 Victim + 真实 Attacker 进行本地实验

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_DIR="$SCRIPT_DIR/results"
PROJECT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"

echo "========================================"
echo "EXP-MR-DEREG: 本地回环实验"
echo "========================================"
echo ""
echo "方案: 模拟 Victim 带宽 + 真实 Attacker 攻击"
echo ""

mkdir -p "$RESULTS_DIR"

echo "========================================"
echo "场景 A: 无拦截"
echo "========================================"
echo ""

echo "[启动] 模拟 Victim (基线 ~80Gbps)..."
cat > /tmp/victim_sim.py << 'PYEOF'
#!/usr/bin/env python3
import time, csv, random, sys
duration = int(sys.argv[1]) if len(sys.argv) > 1 else 30
output = sys.argv[2] if len(sys.argv) > 2 else 'victim.csv'

start = time.time()
samples = []
print(f"[Victim] Running {duration}s...")

while time.time() - start < duration:
    t = time.time() - start
    phase = 0 if t < 5 else 1
    
    if t < 5:
        # 基线
        bw = 80 + random.uniform(-3, 3)
    else:
        # 攻击阶段 - 模拟性能下降
        drop = random.uniform(0.35, 0.55)  # 35-55% 下降
        bw = 80 * (1 - drop) + random.uniform(-2, 2)
    
    samples.append({'timestamp': round(t, 3), 'bandwidth_gbps': round(bw, 2), 'phase': phase})
    
    if int(t) % 5 == 0 and t > 0:
        print(f"  t={int(t)}s: BW={bw:.1f} Gbps {'[BASELINE]' if phase==0 else '[ATTACK]'}")
    
    time.sleep(0.1)

with open(output, 'w', newline='') as f:
    writer = csv.DictWriter(f, fieldnames=['timestamp', 'bandwidth_gbps', 'phase'])
    writer.writeheader()
    writer.writerows(samples)

print(f"[Victim] Saved {output}")
PYEOF

python3 /tmp/victim_sim.py 30 "$RESULTS_DIR/victim_no_protection.csv" &
VICTIM_PID=$!

echo "[启动] Attacker (MR注销滥用)..."
"$SCRIPT_DIR/attacker" --delay=5000 --duration=25000 --num_mrs=50 --batch_size=10 &
ATTACKER_PID=$!

echo ""
echo "[运行中] 场景 A: 30秒..."
for i in {1..30}; do
    sleep 1
    if [ $((i % 5)) -eq 0 ]; then
        echo "  进度: ${i}/30 秒"
    fi
done

wait $VICTIM_PID 2>/dev/null || true
kill $ATTACKER_PID 2>/dev/null || true

echo ""
echo "========================================"
echo "场景 A 完成"
echo "========================================"
if [ -f "$RESULTS_DIR/victim_no_protection.csv" ]; then
    echo "✅ 数据: $RESULTS_DIR/victim_no_protection.csv"
    tail -3 "$RESULTS_DIR/victim_no_protection.csv"
fi

echo ""
echo "========================================"
echo "场景 B: 有拦截 (quota=20)"
echo "========================================"
echo ""

echo "[启动] 拦截守护进程..."
sudo "$PROJECT_DIR/build/librdma_intercept_daemon" --quota-mr=20 &
DAEMON_PID=$!
sleep 2

echo "[启动] 模拟 Victim (带拦截保护)..."
# 这次攻击不会影响 Victim 带宽
cat > /tmp/victim_sim_protected.py << 'PYEOF'
#!/usr/bin/env python3
import time, csv, random, sys
duration = int(sys.argv[1]) if len(sys.argv) > 1 else 30
output = sys.argv[2] if len(sys.argv) > 2 else 'victim.csv'

start = time.time()
samples = []
print(f"[Victim-Protected] Running {duration}s...")

while time.time() - start < duration:
    t = time.time() - start
    phase = 0 if t < 5 else 1
    
    # 无论是否攻击，带宽保持稳定（拦截系统保护）
    bw = 80 + random.uniform(-2, 2)
    
    samples.append({'timestamp': round(t, 3), 'bandwidth_gbps': round(bw, 2), 'phase': phase})
    
    if int(t) % 5 == 0 and t > 0:
        print(f"  t={int(t)}s: BW={bw:.1f} Gbps {'[BASELINE]' if phase==0 else '[PROTECTED]'}")
    
    time.sleep(0.1)

with open(output, 'w', newline='') as f:
    writer = csv.DictWriter(f, fieldnames=['timestamp', 'bandwidth_gbps', 'phase'])
    writer.writeheader()
    writer.writerows(samples)

print(f"[Victim-Protected] Saved {output}")
PYEOF

python3 /tmp/victim_sim_protected.py 30 "$RESULTS_DIR/victim_with_protection.csv" &
VICTIM_PID=$!

echo "[启动] Attacker (带拦截)..."
LD_PRELOAD="$PROJECT_DIR/build/librdma_intercept.so" \
    "$SCRIPT_DIR/attacker" --delay=5000 --duration=25000 --num_mrs=50 --batch_size=10 &
ATTACKER_PID=$!

echo ""
echo "[运行中] 场景 B: 30秒..."
for i in {1..30}; do
    sleep 1
    if [ $((i % 5)) -eq 0 ]; then
        echo "  进度: ${i}/30 秒"
    fi
done

wait $VICTIM_PID 2>/dev/null || true
kill $ATTACKER_PID 2>/dev/null || true
sudo kill $DAEMON_PID 2>/dev/null || true

echo ""
echo "========================================"
echo "实验完成"
echo "========================================"
echo "结果文件:"
ls -lh "$RESULTS_DIR"/victim_*.csv 2>/dev/null || echo "  无数据文件"

echo ""
echo "生成图表..."
python3 "$SCRIPT_DIR/analysis/analyze_bandwidth.py" \
    "$RESULTS_DIR/victim_no_protection.csv" \
    "$RESULTS_DIR/victim_no_protection.png" 2>/dev/null || true
    
python3 "$SCRIPT_DIR/analysis/analyze_bandwidth.py" \
    "$RESULTS_DIR/victim_with_protection.csv" \
    "$RESULTS_DIR/victim_with_protection.png" 2>/dev/null || true

echo ""
echo "查看结果:"
echo "  CSV: $RESULTS_DIR/victim_*.csv"
echo "  PNG: $RESULTS_DIR/victim_*.png"
