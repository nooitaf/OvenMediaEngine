#include "file_private.h"
#include "file_stream.h"
#include "file_session.h"
#include "base/publisher/application.h"
#include "base/publisher/stream.h"

#include <regex>

std::shared_ptr<FileStream> FileStream::Create(const std::shared_ptr<pub::Application> application,
											 const info::Stream &info)
{
	auto stream = std::make_shared<FileStream>(application, info);
	if(!stream->Start())
	{
		return nullptr;
	}
	return stream;
}

FileStream::FileStream(const std::shared_ptr<pub::Application> application,
					 const info::Stream &info)
		: Stream(application, info),
		_writer(nullptr)
{
}

FileStream::~FileStream()
{
	logtd("FileStream(%s/%s) has been terminated finally", 
		GetApplicationName() , GetName().CStr());
}

bool FileStream::Start()
{
	logtd("FileStream(%ld) has been started", GetId());

	//-----------------
	// for TEST
//	std::vector<int32_t> selected_tracks;
//	RecordStart(selected_tracks);
	//-----------------

	return Stream::Start();
}

bool FileStream::Stop()
{
	logtd("FileStream(%u) has been stopped", GetId());

	//-----------------
	// for TEST
//	RecordStop();
	//-----------------

	return Stream::Stop();
}


void FileStream::RecordStart(std::vector<int32_t> selected_tracks)
{
	_writer = FileWriter::Create();

	ov::String tmp_output_fullpath = GetOutputTempFilePath();
	ov::String tmp_output_directory = ov::PathManager::ExtractPath(tmp_output_fullpath).CStr();

	logtd("Temp output path : %s", tmp_output_fullpath.CStr());
	logtd("Temp output directory : %s", tmp_output_directory.CStr());

	// Create temporary directory
	if(MakeDirectoryRecursive(tmp_output_directory.CStr()) == false)
	{
		logte("Could not create directory. path(%s)", tmp_output_directory.CStr());
		
		return;
	}

	// Record it on a temporary path.
	if(_writer->SetPath(tmp_output_fullpath, "mpegts") == false)
	{
		_writer = nullptr;

		return;
	}

	for(auto &track_item : _tracks)
	{
		auto &track = track_item.second;

		// If the selected track list exists. if the current trackid does not exist on the list, ignore it.
		// If no track list is selected, save all tracks.
		if( (selected_tracks.empty() != true) && 
			(std::find(selected_tracks.begin(), selected_tracks.end(), track->GetId()) == selected_tracks.end()) )
		{
			continue;
		}

		auto quality = FileTrackQuality::Create();

		quality->SetCodecId( track->GetCodecId() );
		quality->SetBitrate( track->GetBitrate() );
		quality->SetTimeBase( track->GetTimeBase() );
		quality->SetWidth( track->GetWidth() );
		quality->SetHeight( track->GetHeight() );
		quality->SetSample( track->GetSample() );
		quality->SetChannel( track->GetChannel() );

		bool ret = _writer->AddTrack(track->GetMediaType(), track->GetId(), quality);
		if(ret == false)
		{
			logte("Failed to add new track");
		}
	}

	if(_writer->Start() == false)
	{
		_writer = nullptr;
	}
}

void FileStream::RecordStop()
{
	if(_writer == nullptr)
		return;

	// End recording.
	_writer->Stop();

	ov::String tmp_output_path = _writer->GetPath();

	// Create directory
	ov::String output_path = GetOutputFilePath();
	ov::String output_direcotry = ov::PathManager::ExtractPath(output_path);

	if(MakeDirectoryRecursive(output_direcotry.CStr()) == false)
	{
		logte("Could not create directory. path(%s)", output_direcotry.CStr());	
		return;
	}

	ov::String info_path = GetOutputFileInfoPath();
	ov::String info_directory = ov::PathManager::ExtractPath(info_path);
	
	if(MakeDirectoryRecursive(info_directory.CStr()) == false)
	{
		logte("Could not create directory. path(%s)", info_directory.CStr());	
		return;
	}

	// Moves temporary files to a user-defined path.
	if(rename( tmp_output_path.CStr() , output_path.CStr() ) != 0)
	{
		logte("Failed to move fiel. %s -> %s", tmp_output_path.CStr() , output_path.CStr());
		return;
	}

	// Add information from the recored file.
	// TODO:

	logtd("File Recording Successful. path(%s)", output_path.CStr());
}


void FileStream::SendVideoFrame(const std::shared_ptr<MediaPacket> &media_packet)
{
	if(_writer == nullptr)
		return;

	bool ret = _writer->PutData(
		media_packet->GetTrackId(), 
		media_packet->GetPts(),
		media_packet->GetDts(), 
		media_packet->GetFlag(), 
		media_packet->GetData());

	if(ret == false)
	{
		logte("Failed to add packet");
	}	
}

void FileStream::SendAudioFrame(const std::shared_ptr<MediaPacket> &media_packet)
{
	if(_writer == nullptr)
		return;

	bool ret = _writer->PutData(
		media_packet->GetTrackId(), 
		media_packet->GetPts(),
		media_packet->GetDts(), 
		media_packet->GetFlag(), 
		media_packet->GetData());

	if(ret == false)
	{
		logte("Failed to add packet");
	}	
}

ov::String FileStream::GetOutputTempFilePath()
{
	auto file_config = GetApplicationInfo().GetConfig().GetPublishers().GetFilePublisher();


	ov::String tmp_path = ConvertMacro(file_config.GetFilePath()) + ".tmp";

	logte("TempFilePath : %s", tmp_path.CStr());

	return tmp_path;
}

ov::String FileStream::GetOutputFilePath()
{
	auto file_config = GetApplicationInfo().GetConfig().GetPublishers().GetFilePublisher();

	logte("FilePath : %s", file_config.GetFilePath().CStr());

	return ConvertMacro(file_config.GetFilePath());
}

ov::String FileStream::GetOutputFileInfoPath()
{
	auto file_config = GetApplicationInfo().GetConfig().GetPublishers().GetFilePublisher();

	logte("FileInfoPath : %s", file_config.GetFileInfoPath().CStr());

	return ConvertMacro(file_config.GetFileInfoPath());
}

bool FileStream::MakeDirectoryRecursive(std::string s, mode_t mode)
{
	size_t pos = 0;
	std::string dir;
	int32_t mdret;

	if(access(s.c_str(), R_OK | W_OK ) == 0)
	{
		return true;
	}


	if (s[s.size() - 1] != '/')
	{
		// force trailing / so we can handle everything in loop
		s += '/';
	}

	while ((pos = s.find_first_of('/', pos)) != std::string::npos)
	{
		dir = s.substr(0, pos++);

		logtd("* %s", dir.c_str());

		if (dir.size() == 0)
			continue; // if leading / first time is 0 length

		if ((mdret = ::mkdir(dir.c_str(), mode)) && errno != EEXIST)
		{
			logtd("* ret : %d", mdret);
			return false;
		}
	}

	if(access(s.c_str(), R_OK | W_OK ) == 0)
	{
		return true;
	}

	return false;
}


ov::String FileStream::ConvertMacro(ov::String src)
{
	auto app_config = GetApplicationInfo().GetConfig();
	auto publishers_config = app_config.GetPublishers();
	auto host_info = GetApplicationInfo().GetHostInfo();

	std::string raw_string = src.CStr();
	ov::String replaced_string = ov::String(raw_string.c_str());


	// =========================================
	// Deifinitino of Macro
	// =========================================
	// ${StartTime:YYYYMMDDhhmmss}
	// ${EndTime:YYYYMMDDhhmmss}
	// 	 YYYY - year
	// 	 MM - month (00~12)
	// 	 DD - day (00~31)
	// 	 hh : hour (0~23)
	// 	 mm : minute (00~59)
	// 	 ss : second (00~59)
	// ${VirtualHost} :  Virtual Host Name
	// ${Application} : Application Name
	// ${Stream} : Stream name
	// ${Sequence} : Sequence number

	std::regex reg_exp("\\$\\{([a-zA-Z0-9:]+)\\}");
	const std::sregex_iterator it_end;
	for (std::sregex_iterator it(raw_string.begin(), raw_string.end(), reg_exp); it != it_end; ++it)
	{
		std::smatch matches = *it; 
		std::string tmp;

		tmp = matches[0];
		ov::String full_match = ov::String(tmp.c_str());

		tmp = matches[1];
		ov::String group = ov::String(tmp.c_str());

		logtd("Full Match(%s) => Group(%s)", full_match.CStr(), group.CStr());

		if(group.IndexOf("VirtualHost") != -1L)
		{
			replaced_string = replaced_string.Replace(full_match, host_info.GetName());
		}
		if(group.IndexOf("Application") != -1L)
		{
			// Delete Prefix virtualhost name. ex) #[VirtualHost]#Application
			ov::String prefix = ov::String::FormatString("#%s#", host_info.GetName().CStr());
			ov::String application_name =  GetApplicationInfo().GetName();
			application_name = application_name.Replace(prefix, "");

			replaced_string = replaced_string.Replace(full_match, application_name);
		}
		if(group.IndexOf("Stream") != -1L)
		{
			replaced_string = replaced_string.Replace(full_match, GetName());
		}
		if(group.IndexOf("Sequence") != -1L)
		{
			replaced_string = replaced_string.Replace(full_match, "0");
		}		
		if(group.IndexOf("StartTime") != -1L)
		{
			time_t now = time(NULL);
			char buff[80];
			ov::String YYYY, MM, DD, hh, mm, ss;

			strftime(buff, sizeof(buff), "%Y", localtime(&now)); YYYY = buff;
			strftime(buff, sizeof(buff), "%m", localtime(&now)); MM = buff;
			strftime(buff, sizeof(buff), "%d", localtime(&now)); DD = buff;
			strftime(buff, sizeof(buff), "%H", localtime(&now)); hh = buff;
			strftime(buff, sizeof(buff), "%M", localtime(&now)); mm = buff;
			strftime(buff, sizeof(buff), "%S", localtime(&now)); ss = buff;			

			ov::String str_time = group;
			str_time = str_time.Replace("StartTime:", "");
			str_time = str_time.Replace("YYYY", YYYY);
			str_time = str_time.Replace("MM", MM);
			str_time = str_time.Replace("DD", DD);
			str_time = str_time.Replace("hh", hh);
			str_time = str_time.Replace("mm", mm);
			str_time = str_time.Replace("ss", ss);

			replaced_string = replaced_string.Replace(full_match, str_time);			
		}
		if(group.IndexOf("EndTime") != -1L)
		{
			time_t now = time(NULL);
			char buff[80];
			ov::String YYYY, MM, DD, hh, mm, ss;

			strftime(buff, sizeof(buff), "%Y", localtime(&now)); YYYY = buff;
			strftime(buff, sizeof(buff), "%m", localtime(&now)); MM = buff;
			strftime(buff, sizeof(buff), "%d", localtime(&now)); DD = buff;
			strftime(buff, sizeof(buff), "%H", localtime(&now)); hh = buff;
			strftime(buff, sizeof(buff), "%M", localtime(&now)); mm = buff;
			strftime(buff, sizeof(buff), "%S", localtime(&now)); ss = buff;			

			ov::String str_time = group;
			str_time = str_time.Replace("EndTime:", "");
			str_time = str_time.Replace("YYYY", YYYY);
			str_time = str_time.Replace("MM", MM);
			str_time = str_time.Replace("DD", DD);
			str_time = str_time.Replace("hh", hh);
			str_time = str_time.Replace("mm", mm);
			str_time = str_time.Replace("ss", ss);

			replaced_string = replaced_string.Replace(full_match, str_time);			
		}		
	}	

	logtd("Regular Expreesion Result : %s", replaced_string.CStr());

	return replaced_string;
}

