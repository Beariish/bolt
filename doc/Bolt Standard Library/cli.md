# cli

This module exists to export some cli-specific functionality and assist with running Bolt as a standalone program. This is never defined in the Bolt standard library, but rather only through the `bolt-cli` executable. Checking for the existance of this module with `meta.find_module()` is highly recommended. Check out `examples/clitools.bolt` for an example.

## Constants
```ts
// Contains all the command-line arguments passed when running this script
cli.args: [string]
```