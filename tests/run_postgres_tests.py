#!/usr/bin/env python3
"""Run the real network tests in disposable PostgreSQL schemas.

Prerequisite: PostgreSQL + psql; TEST_DB_CONN_STR must point to a DATABASE
that the testing role owns. Does not touch the production auction schema.
"""
import os
import secrets
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def run(cmd, env):
    print('$', ' '.join(map(str, cmd)))
    subprocess.run(list(map(str, cmd)), check=True, env=env, cwd=ROOT)


def pg(query, conn, env):
    subprocess.run(['psql', '-X', conn, '-v', 'ON_ERROR_STOP=1', '-c', query],
                   check=True, env=env, cwd=ROOT, stdout=subprocess.DEVNULL)


def suite(db_conn):
    schema = 'auction_test_' + secrets.token_hex(6)
    env = os.environ.copy()
    env.pop('PGOPTIONS', None)
    pg(f'CREATE SCHEMA {schema}', db_conn, env)
    try:
        env['PGOPTIONS'] = f'-csearch_path={schema}'
        env['DB_CONN_STR'] = db_conn
        run(['psql', '-X', db_conn, '-v', 'ON_ERROR_STOP=1', '-f', 'database/schema.sql'], env)
        run([sys.executable, 'tests/integration_test.py'], env)
        # Verify database persistence and logging independently of the server's responses.
        for sql, result in [
            ("SELECT count(*) FROM users", '4'),
            ("SELECT count(*) FROM rooms", '3'),
            ("SELECT count(*) FROM bids", '4'),
            ("SELECT count(*) FROM items WHERE status='SOLD'", '4'),
            ("SELECT count(*) FROM activity_logs WHERE action LIKE 'SOLD_%'", '4'),
            ("SELECT count(*) FROM users WHERE password_hash_hex='TestPass123'", '0'),
        ]:
            out = subprocess.check_output(['psql', '-X', db_conn, '-At', '-c', sql],
                                          env=env, cwd=ROOT, text=True).strip()
            assert out == result, f'Unexpected query result: {sql} => {out} (expected {result})'
        print('PASS: PostgreSQL tables contain correct users, rooms, bids, sales and logs')
    finally:
        env.pop('PGOPTIONS', None)
        pg(f'DROP SCHEMA {schema} CASCADE', db_conn, env)


def main():
    conn = os.environ.get('TEST_DB_CONN_STR')
    if not conn:
        raise SystemExit('Set TEST_DB_CONN_STR to a dedicated test database connection string')
    if not shutil.which('psql'):
        raise SystemExit('psql was not found (install postgresql-client)')
    suite(conn)
    print('POSTGRESQL TEST SUITE COMPLETE')


if __name__ == '__main__':
    main()
