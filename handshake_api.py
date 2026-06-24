# 🤝 HANDSHAKE API - Bank Transfer
# Connects mining earnings to your local bank

import json
import requests
from flask import Flask, request, jsonify

app = Flask(__name__)

WALLET = "45ktWDeTNtUcVMXfJRKS6bbXMznMAStZFX6niJHcVy9uQk132bHJ21QTC5AKvqyx9XJN5e7mPc3vViyGnB2BM6DD1ZoAoZb"
BANK_ACCOUNT = "Your Local Bank Account"

@app.route('/balance', methods=['GET'])
def get_balance():
    # Fetch from blockchain
    url = f"https://xmrchain.net/api/address/{WALLET}"
    resp = requests.get(url)
    data = resp.json()
    balance = data.get('balance', 0) / 1000000000000
    return jsonify({"wallet": WALLET, "balance": balance, "bank": BANK_ACCOUNT})

@app.route('/transfer', methods=['POST'])
def transfer():
    # Convert XMR to local currency (THB)
    amount = request.json.get('amount', 0)
    rate = 5500  # XMR to THB
    thb = amount * rate
    return jsonify({"status": "pending", "amount": amount, "thb": thb, "bank": BANK_ACCOUNT})

if __name__ == '__main__':
    app.run(port=5000)
