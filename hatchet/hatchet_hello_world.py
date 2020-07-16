#!/usr/bin/env python3
import hatchet as ht
import numpy as np
import pandas as pd
import sys, pprint

pd.set_option('display.max_rows', 500)
pd.set_option('display.max_columns', 2**10)
pd.set_option('expand_frame_repr', False)

def main( args=sys.argv[1:] ):
  if len(args) == 0:
    print("No database path provided")

  name='name'
  time = "CPUTIME (sec) (E)"
  imbalance='imbalance'
  important_keys = [time]

  gf = ht.GraphFrame.from_hpctoolkit( args[0] )

  # print(gf.tree(metric=time))

  gf.drop_index_levels()
  grouped = gf.dataframe.groupby(name).sum()
  time_sorted = grouped.sort_values( by=[time], ascending=False )[important_keys]
  # print(time_sorted)
  # print("="*100)

  gf = ht.GraphFrame.from_hpctoolkit( args[0] )
  gf1 = gf.deepcopy()
  gf2 = gf.deepcopy()

  print( gf.dataframe )
  pprint.pprint( list(gf.dataframe.index) )

  gf.drop_index_levels()
  gf1.drop_index_levels( function=np.mean )
  gf2.drop_index_levels( function=np.max )

  # gf.dataframe["mean"] = gf1.dataframe[time]
  # gf.dataframe["max"]  = gf2.dataframe[time]
  #
  # gf.dataframe[imbalance] = gf.dataframe[time] / gf.dataframe[time]
  #
  # imbalance_sorted = gf.dataframe.sort_values(by=[imbalance], ascending=False)[[*important_keys, name, imbalance, "mean", "max"]].set_index(name)

  # print( "gf1" )
  # print( gf1.dataframe )
  # print( "gf2" )
  # print( gf2.dataframe )

  gf.dataframe["mean"] = gf1.dataframe[time]
  gf.dataframe["max"]  = gf2.dataframe[time]

  # gf.dataframe[imbalance] = gf2.dataframe[time].div(gf1.dataframe[time])
  gf.dataframe[imbalance] = gf.dataframe["max"].div( gf.dataframe["mean"] )

  imbalance_sorted = gf.dataframe.sort_values(by=[imbalance], ascending=False)[[*important_keys, "name", imbalance, "mean", "max"]] #.set_index("name")
  # print(imbalance_sorted)
  pprint.pprint( list(imbalance_sorted.index) )


if __name__ == "__main__":
  main()
