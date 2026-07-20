using Microsoft.Win32.SafeHandles;
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Threading.Tasks;

namespace GenericHid
{
	/// <summary>
	/// Minimal WinUSB transport for the STM32 vendor-specific Bulk interface.
	/// The interface GUID is also published by the firmware's Microsoft OS descriptors.
	/// </summary>
	internal sealed class WinUsbDevice : IDisposable
	{
		internal static readonly Guid InterfaceGuid =
			new Guid("8B4B6B6A-3266-4A18-AC3B-7110B602D0A3");

		private const Byte ExpectedBulkOutPipe = 0x01;
		private const Byte ExpectedBulkInPipe = 0x81;
		private const Int32 FileFlagOverlapped = 0x40000000;
		private const UInt32 PipeTransferTimeout = 3;
		private const UInt32 TransferTimeoutMilliseconds = 1000;

		private readonly Object _syncRoot = new Object();
		private SafeFileHandle _deviceHandle;
		private IntPtr _winUsbHandle;
		private Byte _bulkOutPipe;
		private Byte _bulkInPipe;

		internal String LastError { get; private set; }

		internal Boolean IsOpen
		{
			get
			{
				return _winUsbHandle != IntPtr.Zero &&
					_deviceHandle != null &&
					!_deviceHandle.IsInvalid &&
					!_deviceHandle.IsClosed;
			}
		}

		internal Boolean Open(String devicePath)
		{
			lock (_syncRoot)
			{
				CloseHandles();
				LastError = String.Empty;

				_deviceHandle = FileIo.CreateFile(
					devicePath,
					FileIo.GenericRead | unchecked((UInt32)FileIo.GenericWrite),
					FileIo.FileShareRead | FileIo.FileShareWrite,
					IntPtr.Zero,
					FileIo.OpenExisting,
					FileFlagOverlapped,
					IntPtr.Zero);

				if (_deviceHandle == null || _deviceHandle.IsInvalid)
				{
					LastError = FormatWin32Error("CreateFile");
					CloseHandles();
					return false;
				}

				if (!NativeMethods.WinUsb_Initialize(_deviceHandle, out _winUsbHandle))
				{
					LastError = FormatWin32Error("WinUsb_Initialize");
					CloseHandles();
					return false;
				}

				if (!FindBulkPipes())
				{
					if (String.IsNullOrEmpty(LastError))
					{
						LastError = "Bulk endpoints 0x01/0x81 not found";
					}
					CloseHandles();
					return false;
				}

				UInt32 timeout = TransferTimeoutMilliseconds;
				Boolean outTimeoutSet = NativeMethods.WinUsb_SetPipePolicy(
					_winUsbHandle,
					_bulkOutPipe,
					PipeTransferTimeout,
					(UInt32)Marshal.SizeOf(typeof(UInt32)),
					ref timeout);
				Boolean inTimeoutSet = NativeMethods.WinUsb_SetPipePolicy(
					_winUsbHandle,
					_bulkInPipe,
					PipeTransferTimeout,
					(UInt32)Marshal.SizeOf(typeof(UInt32)),
					ref timeout);
				if (!outTimeoutSet || !inTimeoutSet)
				{
					/* The default WinUSB transfer path remains usable without this policy. */
					LastError = "Connected; WinUSB timeout policy is unavailable";
				}

				return true;
			}
		}

		internal Task<Int32> WriteAsync(Byte[] buffer)
		{
			return Task.Run(() => Write(buffer));
		}

		internal Task<Int32> ReadAsync(Byte[] buffer)
		{
			return Task.Run(() => Read(buffer));
		}

		private Int32 Write(Byte[] buffer)
		{
			if (buffer == null)
			{
				throw new ArgumentNullException("buffer");
			}

			lock (_syncRoot)
			{
				EnsureOpen();
				UInt32 transferred;
				if (!NativeMethods.WinUsb_WritePipe(
					_winUsbHandle,
					_bulkOutPipe,
					buffer,
					(UInt32)buffer.Length,
					out transferred,
					IntPtr.Zero))
				{
					Int32 error = Marshal.GetLastWin32Error();
					throw new Win32Exception(error,
						"WinUSB Bulk OUT failed, error " + error + ": " +
						new Win32Exception(error).Message);
				}

				return checked((Int32)transferred);
			}
		}

		private Int32 Read(Byte[] buffer)
		{
			if (buffer == null)
			{
				throw new ArgumentNullException("buffer");
			}

			lock (_syncRoot)
			{
				EnsureOpen();
				UInt32 transferred;
				if (!NativeMethods.WinUsb_ReadPipe(
					_winUsbHandle,
					_bulkInPipe,
					buffer,
					(UInt32)buffer.Length,
					out transferred,
					IntPtr.Zero))
				{
					Int32 error = Marshal.GetLastWin32Error();
					throw new Win32Exception(error,
						"WinUSB Bulk IN failed, error " + error + ": " +
						new Win32Exception(error).Message);
				}

				return checked((Int32)transferred);
			}
		}

		private Boolean FindBulkPipes()
		{
			NativeMethods.USB_INTERFACE_DESCRIPTOR descriptor;
			if (!NativeMethods.WinUsb_QueryInterfaceSettings(
				_winUsbHandle, 0, out descriptor))
			{
				LastError = FormatWin32Error("WinUsb_QueryInterfaceSettings");
				return false;
			}

			_bulkOutPipe = 0;
			_bulkInPipe = 0;
			for (Byte index = 0; index < descriptor.bNumEndpoints; index++)
			{
				NativeMethods.WINUSB_PIPE_INFORMATION pipe;
				if (!NativeMethods.WinUsb_QueryPipe(
					_winUsbHandle, 0, index, out pipe))
				{
					LastError = FormatWin32Error("WinUsb_QueryPipe");
					return false;
				}

				if (pipe.PipeType != NativeMethods.USBD_PIPE_TYPE.UsbdPipeTypeBulk)
				{
					continue;
				}

				if ((pipe.PipeId & 0x80) != 0)
				{
					_bulkInPipe = pipe.PipeId;
				}
				else
				{
					_bulkOutPipe = pipe.PipeId;
				}
			}

			return _bulkOutPipe == ExpectedBulkOutPipe &&
				_bulkInPipe == ExpectedBulkInPipe;
		}

		private static String FormatWin32Error(String operation)
		{
			Int32 error = Marshal.GetLastWin32Error();
			return operation + " failed, error " + error + ": " +
				new Win32Exception(error).Message;
		}

		private void EnsureOpen()
		{
			if (!IsOpen)
			{
				throw new InvalidOperationException("WinUSB device is not open");
			}
		}

		public void Dispose()
		{
			lock (_syncRoot)
			{
				CloseHandles();
			}
			GC.SuppressFinalize(this);
		}

		private void CloseHandles()
		{
			if (_winUsbHandle != IntPtr.Zero)
			{
				NativeMethods.WinUsb_Free(_winUsbHandle);
				_winUsbHandle = IntPtr.Zero;
			}

			if (_deviceHandle != null)
			{
				_deviceHandle.Dispose();
				_deviceHandle = null;
			}

			_bulkOutPipe = 0;
			_bulkInPipe = 0;
		}

		private static class NativeMethods
		{
			internal enum USBD_PIPE_TYPE
			{
				UsbdPipeTypeControl,
				UsbdPipeTypeIsochronous,
				UsbdPipeTypeBulk,
				UsbdPipeTypeInterrupt
			}

			[StructLayout(LayoutKind.Sequential, Pack = 1)]
			internal struct USB_INTERFACE_DESCRIPTOR
			{
				internal Byte bLength;
				internal Byte bDescriptorType;
				internal Byte bInterfaceNumber;
				internal Byte bAlternateSetting;
				internal Byte bNumEndpoints;
				internal Byte bInterfaceClass;
				internal Byte bInterfaceSubClass;
				internal Byte bInterfaceProtocol;
				internal Byte iInterface;
			}

			[StructLayout(LayoutKind.Sequential)]
			internal struct WINUSB_PIPE_INFORMATION
			{
				internal USBD_PIPE_TYPE PipeType;
				internal Byte PipeId;
				internal UInt16 MaximumPacketSize;
				internal Byte Interval;
			}

			[DllImport("winusb.dll", SetLastError = true)]
			[return: MarshalAs(UnmanagedType.Bool)]
			internal static extern Boolean WinUsb_Initialize(
				SafeFileHandle deviceHandle,
				out IntPtr interfaceHandle);

			[DllImport("winusb.dll", SetLastError = true)]
			[return: MarshalAs(UnmanagedType.Bool)]
			internal static extern Boolean WinUsb_Free(IntPtr interfaceHandle);

			[DllImport("winusb.dll", SetLastError = true)]
			[return: MarshalAs(UnmanagedType.Bool)]
			internal static extern Boolean WinUsb_QueryInterfaceSettings(
				IntPtr interfaceHandle,
				Byte alternateInterfaceNumber,
				out USB_INTERFACE_DESCRIPTOR usbAltInterfaceDescriptor);

			[DllImport("winusb.dll", SetLastError = true)]
			[return: MarshalAs(UnmanagedType.Bool)]
			internal static extern Boolean WinUsb_QueryPipe(
				IntPtr interfaceHandle,
				Byte alternateInterfaceNumber,
				Byte pipeIndex,
				out WINUSB_PIPE_INFORMATION pipeInformation);

			[DllImport("winusb.dll", SetLastError = true)]
			[return: MarshalAs(UnmanagedType.Bool)]
			internal static extern Boolean WinUsb_SetPipePolicy(
				IntPtr interfaceHandle,
				Byte pipeId,
				UInt32 policyType,
				UInt32 valueLength,
				ref UInt32 value);

			[DllImport("winusb.dll", SetLastError = true)]
			[return: MarshalAs(UnmanagedType.Bool)]
			internal static extern Boolean WinUsb_WritePipe(
				IntPtr interfaceHandle,
				Byte pipeId,
				Byte[] buffer,
				UInt32 bufferLength,
				out UInt32 lengthTransferred,
				IntPtr overlapped);

			[DllImport("winusb.dll", SetLastError = true)]
			[return: MarshalAs(UnmanagedType.Bool)]
			internal static extern Boolean WinUsb_ReadPipe(
				IntPtr interfaceHandle,
				Byte pipeId,
				Byte[] buffer,
				UInt32 bufferLength,
				out UInt32 lengthTransferred,
				IntPtr overlapped);
		}
	}
}
