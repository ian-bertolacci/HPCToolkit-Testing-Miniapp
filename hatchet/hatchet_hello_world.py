#!/usr/bin/env python3
import hatchet as ht
import sys


def main( args=sys.argv[1:] ):
  if len(args) == 0:
    print("No database path provided")

  gf = ht.GraphFrame.from_hpctoolkit( args[0] )

  print(gf.dataframe)
  print(gf.tree())


if __name__ == "__main__":
  main()
