#!/bin/bash
# genkey.sh — create the RSA-2048 knock keypair for the HU knockd gate.
#
# Run this YOURSELF (not the assistant): the private key never needs to appear
# in any chat/terminal output. Only the .pub.pem crosses to the HU.
#
#   ./genkey.sh            # creates ~/.ssh/nbt_knock.pem + .pub.pem
#
set -euo pipefail
KEY=${KNOCK_KEY:-$HOME/.ssh/nbt_knock.pem}

if [ -f "$KEY" ]; then
    echo "key already exists: $KEY — delete it first to rotate" >&2
    exit 1
fi

# Optional passphrase: run with KNOCK_PASS set, or leave empty (key is PM-backed anyway)
if [ -n "${KNOCK_PASS:-}" ]; then
    openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 \
        -aes-256-cbc -pass "env:KNOCK_PASS" -out "$KEY"
else
    openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out "$KEY"
fi
chmod 600 "$KEY"

# public key for the HU knockd — PKCS#1 "RSA PUBLIC KEY" PEM form
openssl rsa -in "$KEY" -RSAPublicKey_out -out "$KEY.pub.pem" ${KNOCK_PASS:+-passin "env:KNOCK_PASS"}
# same key in OpenSSH authorized_keys format — appended to /etc/ssh/authorized_keys
ssh-keygen -y -f "$KEY" ${KNOCK_PASS:+-P "$KNOCK_PASS"} > "$KEY.pub"

cat <<EOF

Done.
  private key : $KEY        — store in your password manager; NEVER paste it
  public key  : $KEY.pub.pem — push to the HU as /var/opt/aa/knock.pub

On the car (via hu.sh):
  hu.sh push $KEY.pub.pem /var/opt/aa/knock.pub

Then knock:
  knock.py <hu-ip>
EOF
