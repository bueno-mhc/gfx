/// @file image_view.cppm
/// @brief ImageView: how shaders and attachments see a range of an image.
export module gfx.interface:image_view;

import std;
import gfx.enums;
import gfx.handles;
import :backend_object;
import :bindless;
import :common_types;

export namespace gfx
{

/// @brief How to create an ImageView.
struct ImageViewDescription
{
    ImageHandle image{};                                ///< Viewed image.
    ImageViewType type = ImageViewType::TWO_DIMENSIONAL;///< How the subresources are interpreted.
    Format format = Format::UNDEFINED;                  ///< View format (UNDEFINED = the image's format).
    ComponentMapping components{};                      ///< Component swizzle.
    ImageSubresourceRange range{};                      ///< Mip levels, layers and aspects viewed.
    bool bindless = true;                               ///< Register it in the bindless table (as a sampled image
                                                        ///< if the image is SAMPLED, as a storage image if STORAGE).
    std::string_view debug_name{};                      ///< Name shown by debugging tools.
};

/// @brief A view of a range of an image, bound to descriptors and used as an attachment.
class ImageView : public BackendObject
{
public:
    /// @brief Handle naming this view in descriptors and attachments.
    [[nodiscard]] virtual auto handle() const noexcept -> ImageViewHandle = 0;

    /// @brief Viewed image.
    [[nodiscard]] virtual auto image() const noexcept -> ImageHandle = 0;

    /// @brief View type.
    [[nodiscard]] virtual auto type() const noexcept -> ImageViewType = 0;

    /// @brief View format.
    [[nodiscard]] virtual auto format() const noexcept -> Format = 0;

    /// @brief Viewed range.
    [[nodiscard]] virtual auto range() const noexcept -> ImageSubresourceRange = 0;

    /// @brief Index in the bindless table's sampled images (SAMPLED image, bindless = true; invalid otherwise).
    [[nodiscard]] virtual auto sampled_index() const noexcept -> SampledImageIndex = 0;

    /// @brief Index in the bindless table's storage images (STORAGE image, bindless = true; invalid otherwise).
    [[nodiscard]] virtual auto storage_index() const noexcept -> StorageImageIndex = 0;
};

} // namespace gfx
