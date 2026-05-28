cd "$(dirname "$0")/.." && REMOTE_PASSWORD=2345 REMOTE_HOST=grande.grad.dcc.ufmg.br REMOTE_PORT=5555 python3 -m pytest helpers/test_e2e.py -v
