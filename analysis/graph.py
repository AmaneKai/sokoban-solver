import os
import matplotlib.pyplot as pyplot
import pandas
import seaborn


def validate_dataset(dataframe: pandas.DataFrame) -> None:
  required_columns = {"map", "status", "moves", "nodes", "time_ms"}
  
  if not required_columns.issubset(dataframe.columns):
    raise ValueError("Dataset is missing required columns")

  if any(dataframe["map"].isnull()):
    raise ValueError("Dataset contains null values in the 'map' column")


def extract_short_map_name(map_name: str) -> str:
  if not map_name:
    return ""

  return map_name.split("_")[0]


def plot_search_effort(
  dataframe: pandas.DataFrame,
  figure_width_inches: int = 12,
  figure_height_inches: int = 6,
  output_image_path: str = "",
) -> None:
  if dataframe.empty:
    return

  pyplot.figure(figsize=(figure_width_inches, figure_height_inches))

  status_palette = {
    "solved": "teal",
    "timeout": "crimson",
    "failed": "darkgray",
  }

  plot_axis = seaborn.scatterplot(
    data=dataframe,
    x="map_index",
    y="nodes",
    hue="status",
    palette=status_palette,
    alpha=0.8,
    edgecolor=None,
    s=25,
  )

  plot_axis.set_yscale("log")
  pyplot.title("Search Effort Profile (Maps Sorted by Nodes Explored)")
  pyplot.xlabel("Maps (Sorted by Difficulty)")
  pyplot.ylabel("Nodes Explored (Log Scale)")

  if len(dataframe) > 30:
    plot_axis.set_xticks([])
  else:
    pyplot.xticks(
      ticks=dataframe["map_index"],
      labels=[str(map_label) for map_label in dataframe["map_short"]],
      rotation=45,
    )

  pyplot.tight_layout()

  if output_image_path:
    pyplot.savefig(output_image_path, dpi=300, bbox_inches="tight")
  else:
    pyplot.show()

  pyplot.close()


def plot_solved_efficiency(
  dataframe: pandas.DataFrame,
  figure_width_inches: int = 12,
  figure_height_inches: int = 6,
  output_image_path: str = "",
) -> None:
  solved_dataframe = dataframe[dataframe["status"] == "solved"].copy()

  if solved_dataframe.empty:
    return

  # type: ignore silences strict stub mismatches on pandas-stubs overloads
  solved_dataframe = solved_dataframe.sort_values(by="nodes").reset_index(drop=True)  # type: ignore

  solved_dataframe["map_index"] = solved_dataframe.index

  _, primary_axis = pyplot.subplots(
    figsize=(figure_width_inches, figure_height_inches)
  )

  primary_color = "tab:blue"
  primary_axis.set_xlabel("Maps (Sorted by Nodes Explored)")
  primary_axis.set_ylabel("Solution Steps (Moves)", color=primary_color)

  primary_axis.plot(
    solved_dataframe["map_index"],
    solved_dataframe["moves"],
    color=primary_color,
    marker="x",
    linestyle="-",
    alpha=0.6,
    label="Moves",
  )
  primary_axis.tick_params(axis="y", labelcolor=primary_color)

  secondary_axis = primary_axis.twinx()
  secondary_color = "tab:orange"
  secondary_axis.set_ylabel("Nodes Explored (Linear Scale)", color=secondary_color)

  secondary_axis.plot(
    solved_dataframe["map_index"],
    solved_dataframe["nodes"],
    color=secondary_color,
    marker="o",
    linestyle="-",
    alpha=0.7,
    label="Nodes",
  )
  secondary_axis.tick_params(axis="y", labelcolor=secondary_color)

  if len(solved_dataframe) > 30:
    primary_axis.set_xticks([])
  else:
    pyplot.xticks(
      ticks=solved_dataframe["map_index"],
      labels=[str(map_label) for map_label in solved_dataframe["map_short"]],
      rotation=45,
    )

  pyplot.title("Solved Maps: Moves vs. Search Effort (Sorted by Nodes)")
  pyplot.tight_layout()

  if output_image_path:
    pyplot.savefig(output_image_path, dpi=300, bbox_inches="tight")
  else:
    pyplot.show()

  pyplot.close()


def run_visualization_pipeline(
  dataset_file_path: str,
  output_directory_path: str = "",
) -> None:
  if not os.path.exists(dataset_file_path):
    raise FileNotFoundError(f"Dataset file not found at: {dataset_file_path}")

  performance_dataframe = pandas.read_csv(dataset_file_path)
  validate_dataset(performance_dataframe)

  performance_dataframe["map_short"] = performance_dataframe["map"].apply(
    extract_short_map_name
  )

  search_effort_output_path = ""
  solved_efficiency_output_path = ""

  if output_directory_path:
    if not os.path.exists(output_directory_path):
      os.makedirs(output_directory_path)
    
    search_effort_output_path = os.path.join(
      output_directory_path, "search_effort_profile.png"
    )
    solved_efficiency_output_path = os.path.join(
      output_directory_path, "solved_maps_efficiency.png"
    )

  # type: ignore silences strict stub mismatches on pandas-stubs overloads
  sorted_dataframe = performance_dataframe.sort_values(by="nodes").reset_index(drop=True)  # type: ignore
  sorted_dataframe["map_index"] = sorted_dataframe.index

  seaborn.set_theme(style="whitegrid")

  plot_search_effort(
    sorted_dataframe,
    output_image_path=search_effort_output_path,
  )
  plot_solved_efficiency(
    performance_dataframe,
    output_image_path=solved_efficiency_output_path,
  )


if __name__ == "__main__":
  run_visualization_pipeline("data/bench_1.csv", output_directory_path="images")
