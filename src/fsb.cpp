/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "convert.h"
#include "utility.h"

#include "lodepng.h"
#include "json.hpp"

using mcpppp::out;

namespace fsb
{
	template<class T>
	static bool compare(T first, T second) noexcept
	{
		return std::abs(first - second) < std::numeric_limits<T>::epsilon();
	}

	// convert red-green-blue color to hue-saturation-value color
	static void rgb2hsv(double& first, double& second, double& third) noexcept
	{
		const double r = first * 20 / 51; // convert 0-255 to 0-100
		const double g = second * 20 / 51;
		const double b = third * 20 / 51;

		const double max = std::max(std::max(r, g), b);
		const double d = max - std::min(std::min(r, g), b);

		// hue
		if (compare(d, 0.0))
		{
			// if r, g, and b are equal, set the hue to 0
			// to prevent dividing by 0
			first = 0;
		}
		else if (compare(max, r))
		{
			first = std::fmod((60 * ((g - b) / d) + 360), 360);
		}
		else if (compare(max, g))
		{
			first = std::fmod((60 * ((b - r) / d) + 120), 360);
		}
		else
		{
			first = std::fmod((60 * ((r - g) / d) + 240), 360);
		}

		// saturation
		if (compare(max, 0.0))
		{
			second = 0;
		}
		else
		{
			second = (d / max) * 100;
		}

		third = max; // value
	}

	// convert hue-saturation-value color to red-green-blue color
	static void hsv2rgb(double& first, double& second, double& third) noexcept
	{
		const double c = second * third / 10000;
		const double x = c * (1 - std::abs(std::fmod((first / 60), 2) - 1));
		const double m = third / 100 - c;

		if (first < 60)
		{
			first = (c + m) * 255; // r
			second = (x + m) * 255; // g
			third = m * 255; // b
		}
		else if (first < 120)
		{
			first = (x + m) * 255;
			second = (c + m) * 255;
			third = m * 255;
		}
		else if (first < 180)
		{
			first = m * 255;
			second = (c + m) * 255;
			third = (x + m) * 255;
		}
		else if (first < 240)
		{
			first = m * 255;
			second = (x + m) * 255;
			third = (c + m) * 255;
		}
		else if (first < 300)
		{
			first = (x + m) * 255;
			second = m * 255;
			third = (c + m) * 255;
		}
		else
		{
			first = (c + m) * 255;
			second = m * 255;
			third = (x + m) * 255;
		}
	}

	// convert black to transparent
	static void convert(std::vector<uint8_t>& image, const unsigned int& w, const unsigned int& h)
	{
		if (!mcpppp::fsbtransparent)
		{
			return;
		}
		for (long long i = 0; i < w; i += 4)
		{
			for (long long j = 0; j < h; j++)
			{
				// if completely opaque
				if (image.at(static_cast<size_t>(w * h - (j + 1) * w + i + 3)) == 255)
				{
					double first = image.at(static_cast<size_t>(w * h - (j + 1) * w + i));
					double second = image.at(static_cast<size_t>(w * h - (j + 1) * w + i + 1));
					double third = image.at(static_cast<size_t>(w * h - (j + 1) * w + i + 2));
					rgb2hsv(first, second, third);
					const double alpha = third * 51 / 20; // convert 0-100 to 0-255
					third = 100;
					hsv2rgb(first, second, third);
					image.at(static_cast<size_t>(w * h - (j + 1) * w + i)) = static_cast<uint8_t>(first);
					image.at(static_cast<size_t>(w * h - (j + 1) * w + i + 1)) = static_cast<uint8_t>(second);
					image.at(static_cast<size_t>(w * h - (j + 1) * w + i + 2)) = static_cast<uint8_t>(third);
					image.at(static_cast<size_t>(w * h - (j + 1) * w + i + 3)) = static_cast<uint8_t>(alpha);
				}
			}
		}
	}

	static constexpr void checkError(const unsigned int& i)
	{
		if (i)
		{
			out(5) << "FSB: png error: " << lodepng_error_text(i) << std::endl;
		}
	}

	// convert optifine image format (1 image for all 6 sides) into fsb image format (1 image per side)
	static void png(const std::string& path, const std::string& output, const std::filesystem::directory_entry& entry)
	{
		out(1) << "FSB: Converting " + entry.path().filename().u8string() << std::endl;
		unsigned int w, h;
		std::vector<uint8_t> buffer, image, image1, image2, image3, top; // before h/2: bottom (rotate 90 counterclockwise), top (rotate 90 clockwise), south; h/2 to h: west, north, east
		// rotation: w*h - w + 1, w*h - 2*w + 1, ..., w*h - h*w + 1, w*h - w + 2, w*h - 2*w + 2, ..., w*h - w + w, w*h - 2*w + w, ...
		std::string filename = entry.path().stem().u8string();
		lodepng::State state;
		state.info_raw.colortype = LCT_RGBA;
		state.info_raw.bitdepth = 8;
		checkError(lodepng::load_file(buffer, entry.path().u8string()));
		checkError(lodepng::decode(image, w, h, state, buffer));
		if (w % 3 != 0 || h % 2 != 0)
		{
			out(4) << "FSB: Wrong dimensions: " << entry.path().u8string() << std::endl << "will be cropped to proper dimensions" << std::endl;
		}
		image1.reserve(buffer.size() / 6);
		image2.reserve(buffer.size() / 6);
		image3.reserve(buffer.size() / 6);
		const unsigned int outw = w / 3 * 4;
		const unsigned int outh = h / 2;
		for (size_t i = 0; i < (w * 4) * outh; i++)
		{
			if (i % (w * 4) < outw)
			{
				image1.push_back(image.at(i));
			}
			else if (i % (w * 4) < 2 * outw)
			{
				image2.push_back(image.at(i));
			}
			else if (i % (w * 4) < 3 * outw)
			{
				image3.push_back(image.at(i));
			}
		}

		convert(image1, outw, outh);
		convert(image2, outw, outh);
		convert(image3, outw, outh);

		top.reserve(image.size() / 6);
		for (long long i = 0; i < outw; i += 4)
		{
			for (long long j = 0; j < outh; j++)
			{
				top.push_back(image2.at(static_cast<size_t>(outw * outh - (j + 1) * outw + i)));
				top.push_back(image2.at(static_cast<size_t>(outw * outh - (j + 1) * outw + i + 1)));
				top.push_back(image2.at(static_cast<size_t>(outw * outh - (j + 1) * outw + i + 2)));
				top.push_back(image2.at(static_cast<size_t>(outw * outh - (j + 1) * outw + i + 3)));
			}
		}
		buffer.clear();
		std::filesystem::create_directories(std::filesystem::u8path(path + output));
		checkError(lodepng::encode(buffer, image1, outw / 4, outh, state));
		checkError(lodepng::save_file(buffer, path + output + filename + "_bottom.png"));
		buffer.clear();
		checkError(lodepng::encode(buffer, top, outh, outw / 4, state));
		checkError(lodepng::save_file(buffer, path + output + filename + "_top.png"));
		buffer.clear();
		checkError(lodepng::encode(buffer, image3, outw / 4, outh, state));
		checkError(lodepng::save_file(buffer, path + output + filename + "_south.png"));
		image1.clear();
		image2.clear();
		image3.clear();
		for (size_t i = (w * 4) * outh; i < (w * 4) * 2 * outh; i++)
		{
			if (i % (w * 4) < outw)
			{
				image1.push_back(image.at(i));
			}
			else if (i % (w * 4) < 2 * outw)
			{
				image2.push_back(image.at(i));
			}
			else if (i % (w * 4) < 3 * outw)
			{
				image3.push_back(image.at(i));
			}
		}

		convert(image1, outw, outh);
		convert(image2, outw, outh);
		convert(image3, outw, outh);
		buffer.clear();
		checkError(lodepng::encode(buffer, image1, outw / 4, outh, state));
		checkError(lodepng::save_file(buffer, path + output + filename + "_west.png"));
		buffer.clear();
		checkError(lodepng::encode(buffer, image2, outw / 4, outh, state));
		checkError(lodepng::save_file(buffer, path + output + filename + "_north.png"));
		buffer.clear();
		checkError(lodepng::encode(buffer, image3, outw / 4, outh, state));
		checkError(lodepng::save_file(buffer, path + output + filename + "_east.png"));
	}

	// convert optifine properties files into fsb properties json
	static void prop(const std::string& path, const std::filesystem::directory_entry& entry)
	{
		int startfadein = -1, endfadein = -1, startfadeout = -1, endfadeout = -1;
		std::string name = entry.path().stem().u8string(), source, option, value, temp;
		std::vector<uint8_t> buffer;
		lodepng::State state;
		state.info_raw.colortype = LCT_RGBA;
		state.info_raw.bitdepth = 8;
		source = name;
		nlohmann::json j =
		{
			{"schemaVersion", 2},
			{"type", "square-textured"},
			{"conditions",
			{
				{"worlds", {"minecraft:overworld"}}
			} },
			{"blend", true},
			{"properties",
			{
				{"blend", {{"type", "add"}}},
				{"rotation",
				{
					{"axis", {0.0, 180.0, 0.0}}
				}},
				{"sunSkyTint", false}
			} }
		};
		std::ifstream fin(entry.path());
		while (fin)
		{
			std::getline(fin, temp);
			option.clear();
			value.clear();
			bool isvalue = false;
			for (const char& c : temp)
			{
				if (c == '=')
				{
					isvalue = true;
				}
				else if (!isvalue)
				{
					option += c;
				}
				else // isvalue
				{
					value += c;
				}
			}
			while (option.back() == ' ' || option.back() == '\t')
			{
				option.pop_back();
			}
			while (value.front() == ' ' || value.front() == '\t')
			{
				value.erase(value.begin());
			}
			if (temp.empty())
			{
				continue;
			}
			if (option == "source")
			{
				source = value;
				source.erase(source.end() - 4, source.end());
			}
			else if (option == "startFadeIn" || option == "startFadeOut" || option == "endFadeIn" || option == "endFadeOut")
			{
				temp = value;
				// apparently \: is valid syntax
				mcpppp::findreplace(temp, "\\:", ":");
				for (size_t i = 0; i < temp.size(); i++)
				{
					if (temp.at(i) == ':')
					{
						temp.erase(temp.begin() + static_cast<std::string::difference_type>(i));
						i--;
					}
				}
				temp += '0';
				try
				{
					int tempi = stoi(temp);
					tempi = tempi / 1000 * 1000 + static_cast<int>(round((tempi % 1000) / 3.0 * 5));
					tempi = (tempi + 18000) % 24000;
					((option == "startFadeIn" || option == "startFadeOut") ?
						(option == "startFadeIn" ? startfadein : startfadeout) :
						(option == "endFadeIn" ? endfadein : endfadeout))
						= tempi;
					j["properties"]["fade"][option] = tempi;
				}
				catch (const std::invalid_argument& e)
				{
					out(5) << "Error: " << e.what() << "\n\tIn file \"" << entry.path().u8string() << "\"\n\t" << "stoi argument is \"" << temp << "\"" << std::endl;
					return;
				}
			}
			else if (option == "blend")
			{
				j["properties"]["blend"]["type"] = value;
			}
			else if (option == "rotate")
			{
				j["properties"]["shouldRotate"] = (value == "true");
			}
			else if (option == "speed")
			{
				try
				{
					j["properties"]["rotation"]["rotationSpeed"] = stod(value);
				}
				catch (const std::invalid_argument& e)
				{
					out(5) << "Error: " << e.what() << "\n\tIn file \"" << entry.path().u8string() << "\"\n\t" << "stod argument is \"" << temp << "\"" << std::endl;
					return;
				}
			}
			else if (option == "axis")
			{
				std::string x, y, z;
				std::stringstream axis;
				axis.str(value);
				axis >> x >> y >> z;
				try
				{
					j["properties"]["rotation"]["axis"] = { stod(x) * 180, stod(y) * 180, stod(z) * 180 };
				}
				catch (const std::invalid_argument& e)
				{
					out(5) << "Error: " << e.what() << "\n\tIn file \"" << entry.path().u8string() << "\"\n\t" << "stod argument is \"" << temp << "\"" << std::endl;
					return;
				}
			}
			else if (option == "weather")
			{
				std::string weather;
				std::stringstream weathers;
				std::vector<std::string> weatherlist;
				weathers.str(value);
				while (weathers)
				{
					weathers >> weather;
					weatherlist.push_back(weather);
				}
				j["conditions"]["weather"] = weatherlist;
			}
			else if (option == "biomes")
			{
				std::string biome;
				std::stringstream biomes;
				std::vector<std::string> biomelist;
				biomes.str(value);
				while (biomes)
				{
					biomes >> biome;
					biomelist.push_back(biome);
				}
				j["conditions"]["biomes"] = biomelist;
			}
			if (option == "heights")
			{
				std::string height, minheight;
				std::stringstream heights;
				std::vector<nlohmann::json> heightlist;
				heights.str(value);
				while (heights)
				{
					height.clear();
					heights >> height;
					for (size_t i = 0; i < height.size(); i++)
					{
						if (height.at(i) == '-')
						{
							minheight = height;
							minheight.erase(minheight.begin() + static_cast<std::string::difference_type>(i), minheight.end());
							height.erase(height.begin(), height.begin() + static_cast<std::string::difference_type>(i) + 1);
							try
							{
								heightlist.push_back(nlohmann::json({ {"min", stod(minheight)}, {"max", stod(height)} }));
							}
							catch (const std::invalid_argument& e)
							{
								out(5) << "Error: " << e.what() << "\n\tIn file \"" << entry.path().u8string() << "\"\n\t" << "stod argument is \"" << temp << "\"" << std::endl;
								return;
							}
						}
					}
				}
				j["conditions"]["heights"] = heightlist;
			}
			if (option == "transition")
			{
				// dunno how this works either lol (will be changed when new ver of fsb is released maybe)
			}
		}
		fin.close();
		j["properties"]["rotation"]["static"] = { 1, 1, 1 };
		if (startfadeout == -1)
		{
			j["properties"]["fade"]["startFadeOut"] = (endfadeout - endfadein + startfadein + 24000) % 24000;
		}
		if (source.at(0) == '.' && source.at(1) == '/')
		{
			source.erase(source.begin());
			std::string origsource = source;
			temp = entry.path().parent_path().generic_u8string();
			if (temp.back() == '/')
			{
				temp.erase(temp.end() - 1);
			}
			temp += source;
			source = "fabricskyboxes:sky" + source;
			std::filesystem::directory_entry image = std::filesystem::directory_entry(std::filesystem::u8path(temp + ".png"));
			if (image.exists())
			{
				png(path, "/assets/fabricskyboxes/sky/", image);
			}
			else
			{
				out(4) << "FSB: File not found: " << temp + ".png" << std::endl;
				lodepng::encode(buffer, { 0, 0, 0, 1 }, 1, 1, state);
				lodepng::save_file(buffer, path + "/assets/fabricskyboxes/sky" + origsource + "_top.png");
				lodepng::save_file(buffer, path + "/assets/fabricskyboxes/sky" + origsource + "_bottom.png");
				lodepng::save_file(buffer, path + "/assets/fabricskyboxes/sky" + origsource + "_north.png");
				lodepng::save_file(buffer, path + "/assets/fabricskyboxes/sky" + origsource + "_south.png");
				lodepng::save_file(buffer, path + "/assets/fabricskyboxes/sky" + origsource + "_west.png");
				lodepng::save_file(buffer, path + "/assets/fabricskyboxes/sky" + origsource + "_east.png");
				buffer.clear();
				buffer.shrink_to_fit();
			}
		}
		else
		{
			std::string sourcefolder = source, sourcefile;
			while (sourcefolder.back() != '/')
			{
				sourcefolder.erase(sourcefolder.end() - 1);
				if (sourcefolder.empty())
				{
					throw std::out_of_range("FSB: source does not contain a /");
				}
			}
			sourcefile = std::string(source.begin() + static_cast<std::string::difference_type>(sourcefolder.size()), source.end());
			if (sourcefolder.front() != '/')
			{
				sourcefolder.insert(sourcefolder.begin(), '/');
			}
			std::filesystem::directory_entry image = std::filesystem::directory_entry(std::filesystem::u8path(path + (source.front() == '/' ? "" : "/") + source + ".png"));
			if (image.exists())
			{
				png(path, "/assets/fabricskyboxes/sky" + sourcefolder, image);
			}
			else
			{
				out(4) << "FSB: File not found: " << sourcefolder + sourcefile + ".png" << std::endl;
				lodepng::encode(buffer, { 0, 0, 0, 1 }, 1, 1, state);
				lodepng::save_file(buffer, path + "/assets/fabricskyboxes/sky" + sourcefolder + sourcefile + "_top.png");
				lodepng::save_file(buffer, path + "/assets/fabricskyboxes/sky" + sourcefolder + sourcefile + "_bottom.png");
				lodepng::save_file(buffer, path + "/assets/fabricskyboxes/sky" + sourcefolder + sourcefile + "_north.png");
				lodepng::save_file(buffer, path + "/assets/fabricskyboxes/sky" + sourcefolder + sourcefile + "_south.png");
				lodepng::save_file(buffer, path + "/assets/fabricskyboxes/sky" + sourcefolder + sourcefile + "_west.png");
				lodepng::save_file(buffer, path + "/assets/fabricskyboxes/sky" + sourcefolder + sourcefile + "_east.png");
				buffer.clear();
				buffer.shrink_to_fit();
			}
			source = "fabricskyboxes:sky" + sourcefolder + sourcefile;
		}
		j["textures"]["top"] = source + "_top.png";
		j["textures"]["bottom"] = source + "_bottom.png";
		j["textures"]["north"] = source + "_north.png";
		j["textures"]["south"] = source + "_south.png";
		j["textures"]["west"] = source + "_west.png";
		j["textures"]["east"] = source + "_east.png";
		if (!std::filesystem::exists(std::filesystem::u8path(path + "/assets/fabricskyboxes/sky/")))
		{
			std::filesystem::create_directories(std::filesystem::u8path(path + "/assets/fabricskyboxes/sky"));
		}
		std::ofstream fout(std::filesystem::u8path(path + "/assets/fabricskyboxes/sky/" + name + ".json"));
		fout << j.dump(1, '\t') << std::endl;
		fout.close();
	}

	mcpppp::checkinfo check(const std::filesystem::path& path, const bool& zip)
	{
		if (mcpppp::findfolder(path.u8string(), "assets/fabricskyboxes/sky/", zip))
		{
			if (mcpppp::autoreconvert)
			{
				out(3) << "FSB: Reconverting " << path.filename().u8string() << std::endl;
				std::filesystem::remove_all(std::filesystem::u8path(path.u8string() + "/assets/fabricskyboxes"));
			}
			else
			{
				out(2) << "FSB: Fabricskyboxes folder found in " << path.filename().u8string() << ", skipping" << std::endl;
				return { false, false, false };
			}
		}
		if (mcpppp::findfolder(path.u8string(), "assets/minecraft/optifine/sky/", zip))
		{
			return { true, true, false };
		}
		else if (mcpppp::findfolder(path.u8string(), "assets/minecraft/mcpatcher/sky/", zip))
		{
			return { true, false, false };
		}
		else
		{
			out(2) << "FSB: Nothing to convert in " << path.filename().u8string() << ", skipping" << std::endl;
			return { false, false, false };
		}
	}

	// main fsb function
	void convert(const std::string& path, const std::string& filename, const mcpppp::checkinfo& info)
	{
		out(3) << "FSB: Converting Pack " << filename << std::endl;
		for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::u8path(path + "/assets/minecraft/" + (info.optifine ? "optifine" : "mcpatcher") + "/sky/world0")))
		{
			if (entry.path().extension() == ".properties")
			{
				out(1) << "FSB: Converting " + entry.path().filename().u8string() << std::endl;
				prop(path, entry);
			}
		}
	}
};
