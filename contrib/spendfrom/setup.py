from distutils.core import setup
setup(name='bobspendfrom',
      version='1.0',
      description='Command-line utility for dobbscoin "coin control"',
      author='Gavin Andresen',
      author_email='gavin@dobbscoinfoundation.org',
      requires=['jsonrpc'],
      scripts=['spendfrom.py'],
      )
