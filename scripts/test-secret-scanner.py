"""Check that email is permitted without suppressing credentials on the same line."""
from pathlib import Path
import shutil
import subprocess
import tempfile

scanner = shutil.which('gitleaks')
if not scanner:
    raise SystemExit('gitleaks is required')
config = str(Path('.gitleaks.toml').resolve())
with tempfile.TemporaryDirectory(prefix='at-pet-secret-contract-') as directory:
    fixture = Path(directory) / 'sample.txt'
    # Construct dummy credentials at runtime; never commit a complete token.
    token = 'ghp_' + 'aB3dE5fG7hI9jK1mN2pQ4rS6tU8vW0xY2zA4'
    for text, expected in [('Contact: maintainer@example.com\n', 0),
                           ('Contact: maintainer@example.com token=' + token + '\n', 1)]:
        fixture.write_text(text, encoding='utf-8')
        result = subprocess.run([scanner, 'dir', directory, '--config', config,
                                 '--redact', '--no-banner', '--exit-code', '42'],
                                capture_output=True, text=True)
        required = 42 if expected else 0
        if result.returncode != required:
            raise SystemExit(f'Secret scanner contract failed (expected {required}, got {result.returncode}).')
print('Email exception and same-line credential detection passed.')
